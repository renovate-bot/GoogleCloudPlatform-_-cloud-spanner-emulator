//
// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "backend/query/query_validator.h"

#include <optional>
#include <string>
#include <vector>

#include "zetasql/public/type.h"
#include "zetasql/resolved_ast/resolved_ast.h"
#include "zetasql/resolved_ast/resolved_node.h"
#include "zetasql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "backend/query/feature_filter/gsql_supported_functions.h"
#include "backend/query/feature_filter/sql_feature_filter.h"
#include "backend/query/query_context.h"
#include "backend/query/query_engine_options.h"
#include "backend/query/queryable_table.h"
#include "backend/schema/catalog/column.h"
#include "backend/schema/catalog/index.h"
#include "backend/schema/catalog/sequence.h"
#include "backend/schema/catalog/table.h"
#include "backend/transaction/read_write_transaction.h"
#include "common/constants.h"
#include "common/errors.h"
#include "zetasql/base/ret_check.h"
#include "zetasql/base/status_macros.h"

namespace google {
namespace spanner {
namespace emulator {
namespace backend {

namespace {

constexpr absl::string_view kSpannerQueryEngineHintPrefix = "spanner";

// Hints related to adaptive execution.
constexpr absl::string_view kHintEnableAdaptivePlans = "enable_adaptive_plans";

// Parameter sensitive plans hint
constexpr absl::string_view kHintParameterSensitive = "parameter_sensitive";
constexpr absl::string_view kHintParameterSensitiveAlways = "always";
constexpr absl::string_view kHintParameterSensitiveAuto = "auto";
constexpr absl::string_view kHintParameterSensitiveNever = "never";

// Indexes
constexpr absl::string_view kHintForceIndex = "force_index";
constexpr absl::string_view kHintBaseTable = "_base_table";

// Index strategy
constexpr absl::string_view kHintIndexStrategy = "index_strategy";
constexpr absl::string_view kHintIndexStrategyForceIndexUnion =
    "force_index_union";

// Joins
constexpr absl::string_view kHintJoinForceOrder = "force_join_order";
constexpr absl::string_view kHintJoinMethod = "join_method";
constexpr absl::string_view kHintJoinTypeApply = "apply_join";
constexpr absl::string_view kHintJoinTypeHash = "hash_join";
constexpr absl::string_view kHintJoinTypeMerge = "merge_join";
constexpr absl::string_view kHintJoinTypePushBroadcastHashJoin =
    "push_broadcast_hash_join";

constexpr absl::string_view kHintJoinBatch = "batch_mode";

// For join_method = hash_join
constexpr absl::string_view kHashJoinBuildSide = "hash_join_build_side";
constexpr absl::string_view kHashJoinBuildSideLeft = "build_left";
constexpr absl::string_view kHashJoinBuildSideRight = "build_right";
constexpr absl::string_view kHashJoinExecution = "hash_join_execution";
constexpr absl::string_view kHashJoinExecutionOnePass = "one_pass";
constexpr absl::string_view kHashJoinExecutionMultiPass = "multi_pass";

// Group by
constexpr absl::string_view kHintGroupMethod = "group_method";
constexpr absl::string_view kHintGroupMethodHash = "hash_group";
constexpr absl::string_view kHintGroupMethodStream = "stream_group";

// Specialized transformations
constexpr absl::string_view kHintConstantFolding = "constant_folding";
constexpr absl::string_view kHintTableScanGroupByScanOptimization =
    "groupby_scan_optimization";

// Runtime hints
constexpr absl::string_view kUseAdditionalParallelism =
    "use_additional_parallelism";

// Lock scanned ranges
constexpr absl::string_view kHintLockScannedRange = "lock_scanned_ranges";
constexpr absl::string_view kHintLockScannedRangeShared = "shared";
constexpr absl::string_view kHintLockScannedRangeExclusive = "exclusive";

constexpr absl::string_view kHintGroupTypeDeprecated = "group_type";

constexpr absl::string_view kHintJoinTypeDeprecated = "join_type";
constexpr absl::string_view kHintJoinTypeNestedLoopDeprecated = "loop_join";

// Emulator-specific hints that can be used to set QueryEngineOptions.
constexpr absl::string_view kEmulatorQueryEngineHintPrefix = "spanner_emulator";

constexpr absl::string_view kHintDisableQueryPartitionabilityCheck =
    "disable_query_partitionability_check";

constexpr absl::string_view kHintDisableQueryNullFilteredIndexCheck =
    "disable_query_null_filtered_index_check";

constexpr absl::string_view kHintDisableInline = "disable_inline";

constexpr absl::string_view kHintAllowSearchIndexesInTransaction =
    "allow_search_indexes_in_transaction";

constexpr absl::string_view kRequireEnhanceQuery = "require_enhance_query";
constexpr absl::string_view kEnhanceQueryTimeoutMs = "enhance_query_timeout_ms";
constexpr absl::string_view kScanMethod = "scan_method";
constexpr absl::string_view kScanMethodBatch = "batch";
constexpr absl::string_view kScanMethodRow = "row";

absl::Status CollectHintsForNode(
    const zetasql::ResolvedOption* hint,
    absl::flat_hash_map<absl::string_view, zetasql::Value>* node_hint_map) {
  ZETASQL_RET_CHECK_EQ(hint->value()->node_kind(), zetasql::RESOLVED_LITERAL);
  const zetasql::Value& value =
      hint->value()->GetAs<zetasql::ResolvedLiteral>()->value();
  if (node_hint_map->contains(hint->name())) {
    return error::MultipleValuesForSameHint(hint->name());
  }
  (*node_hint_map)[hint->name()] = value;
  return absl::OkStatus();
}

}  // namespace

bool IsSearchQueryAllowed(const QueryEngineOptions* options,
                          const QueryContext& context) {
  bool allow_search_indexes_in_transaction =
      options != nullptr && options->allow_search_indexes_in_transaction;
  if (!allow_search_indexes_in_transaction) {
    if (context.writer != nullptr &&
        dynamic_cast<const ReadWriteTransaction*>(context.writer) != nullptr) {
      // Not allowed in ReadWriteTransaction.
      return false;
    }
  }

  return true;
}

bool IsSelectForUpdateQuery(const zetasql::ResolvedNode& node) {
  std::vector<const zetasql::ResolvedNode*> scan_nodes;
  node.GetDescendantsSatisfying(
      &zetasql::ResolvedNode::Is<zetasql::ResolvedLockMode>, &scan_nodes);
  return !scan_nodes.empty();
}

absl::Status QueryValidator::ValidateHints(
    const zetasql::ResolvedNode* node) {
  std::vector<const zetasql::ResolvedNode*> child_nodes;
  node->GetChildNodes(&child_nodes);
  // Process the hints for each node, using maps to keep track of
  // the hints for each node.
  absl::flat_hash_map<absl::string_view, zetasql::Value> hint_map;
  absl::flat_hash_map<absl::string_view, zetasql::Value> emulator_hint_map;
  for (const zetasql::ResolvedNode* child_node : child_nodes) {
    if (child_node->node_kind() == zetasql::RESOLVED_OPTION) {
      const zetasql::ResolvedOption* hint =
          child_node->GetAs<zetasql::ResolvedOption>();
      if (absl::EqualsIgnoreCase(hint->qualifier(),
                                 kSpannerQueryEngineHintPrefix) ||
          hint->qualifier().empty()) {
        ZETASQL_RETURN_IF_ERROR(CheckSpannerHintName(hint->name(), node->node_kind()));
        ZETASQL_RETURN_IF_ERROR(CollectHintsForNode(hint, &hint_map));
      } else if (absl::EqualsIgnoreCase(hint->qualifier(),
                                        kEmulatorQueryEngineHintPrefix)) {
        ZETASQL_RETURN_IF_ERROR(CheckEmulatorHintName(hint->name(), node->node_kind()));
        ZETASQL_RETURN_IF_ERROR(CollectHintsForNode(hint, &emulator_hint_map));
      } else {
        // Ignore hints intended for other engines. Mark the value used so an
        // 'Unimplemented' error is not raised.
        hint->value()->MarkFieldsAccessed();
        continue;
      }
    }
  }

  for (const auto& [hint_name, hint_value] : hint_map) {
    ZETASQL_RETURN_IF_ERROR(
        CheckHintValue(hint_name, hint_value, node->node_kind(), hint_map));
  }

  ZETASQL_RETURN_IF_ERROR(ExtractSpannerOptionsForNode(hint_map));

  // Extract any Emulator-engine options from the hints for this node.
  return ExtractEmulatorOptionsForNode(emulator_hint_map);
}

absl::Status QueryValidator::CheckSpannerHintName(
    absl::string_view name, const zetasql::ResolvedNodeKind node_kind) const {
  static const auto* supported_hints = new const absl::flat_hash_map<
      zetasql::ResolvedNodeKind,
      absl::flat_hash_set<absl::string_view, zetasql_base::StringViewCaseHash,
                          zetasql_base::StringViewCaseEqual>>{
      {zetasql::RESOLVED_TABLE_SCAN,
       {kHintForceIndex, kHintTableScanGroupByScanOptimization,
        kHintIndexStrategy, kScanMethod}},
      {zetasql::RESOLVED_JOIN_SCAN,
       {kHintJoinTypeDeprecated, kHintJoinMethod, kHashJoinBuildSide,
        kHintJoinForceOrder, kHashJoinExecution}},
      {zetasql::RESOLVED_AGGREGATE_SCAN,
       {kHintGroupTypeDeprecated, kHintGroupMethod}},
      {zetasql::RESOLVED_ARRAY_SCAN,
       {kHintJoinTypeDeprecated, kHintJoinMethod, kHashJoinBuildSide,
        kHintJoinBatch, kHintJoinForceOrder}},
      {zetasql::RESOLVED_QUERY_STMT,
       {
           kHintForceIndex,
           kHintIndexStrategy,
           kHintJoinTypeDeprecated,
           kHintJoinMethod,
           kHashJoinBuildSide,
           kHintJoinForceOrder,
           kHintConstantFolding,
           kUseAdditionalParallelism,
           kHintEnableAdaptivePlans,
           kHintLockScannedRange,
           kHintParameterSensitive,
           kHashJoinExecution,
           kHintAllowSearchIndexesInTransaction,
           kRequireEnhanceQuery,
           kEnhanceQueryTimeoutMs,
           kScanMethod,
       }},
      {zetasql::RESOLVED_SUBQUERY_EXPR,
       {kHintJoinTypeDeprecated, kHintJoinMethod, kHashJoinBuildSide,
        kHintJoinBatch, kHintJoinForceOrder, kHashJoinExecution}},
      {zetasql::RESOLVED_SET_OPERATION_SCAN,
       {kHintJoinMethod, kHintJoinForceOrder}},
      {zetasql::RESOLVED_FUNCTION_CALL, {kHintDisableInline}}};

  const auto& iter = supported_hints->find(node_kind);
  if (iter == supported_hints->end() || !iter->second.contains(name)) {
    return error::InvalidHint(name);
  }
  return absl::OkStatus();
}

absl::Status QueryValidator::CheckEmulatorHintName(
    absl::string_view name, const zetasql::ResolvedNodeKind node_kind) const {
  static const auto* supported_hints = new const absl::flat_hash_map<
      zetasql::ResolvedNodeKind,
      absl::flat_hash_set<absl::string_view, zetasql_base::StringViewCaseHash,
                          zetasql_base::StringViewCaseEqual>>{
      {zetasql::RESOLVED_TABLE_SCAN,
       {kHintDisableQueryNullFilteredIndexCheck}},
      {zetasql::RESOLVED_QUERY_STMT,
       {kHintDisableQueryPartitionabilityCheck,
        kHintDisableQueryNullFilteredIndexCheck}},
  };

  const auto& iter = supported_hints->find(node_kind);
  if (iter == supported_hints->end() || !iter->second.contains(name)) {
    return error::InvalidEmulatorHint(name);
  }
  return absl::OkStatus();
}

absl::Status QueryValidator::CheckHintValue(
    absl::string_view name, const zetasql::Value& value,
    const zetasql::ResolvedNodeKind node_kind,
    const absl::flat_hash_map<absl::string_view, zetasql::Value>& hint_map) {
  static const auto* supported_hint_types =
      new absl::flat_hash_map<absl::string_view, const zetasql::Type*,
                              zetasql_base::StringViewCaseHash, zetasql_base::StringViewCaseEqual>{{
          {kHintJoinTypeDeprecated, zetasql::types::StringType()},
          {kHintParameterSensitive, zetasql::types::StringType()},
          {kHintJoinMethod, zetasql::types::StringType()},
          {kHashJoinBuildSide, zetasql::types::StringType()},
          {kHashJoinExecution, zetasql::types::StringType()},
          {kHintJoinBatch, zetasql::types::BoolType()},
          {kHintJoinForceOrder, zetasql::types::BoolType()},
          {kHintGroupTypeDeprecated, zetasql::types::StringType()},
          {kHintGroupMethod, zetasql::types::StringType()},
          {kHintForceIndex, zetasql::types::StringType()},
          {kUseAdditionalParallelism, zetasql::types::BoolType()},
          {kHintLockScannedRange, zetasql::types::StringType()},
          {kHintConstantFolding, zetasql::types::BoolType()},
          {kHintTableScanGroupByScanOptimization, zetasql::types::BoolType()},
          {kHintEnableAdaptivePlans, zetasql::types::BoolType()},
          {kHintDisableInline, zetasql::types::BoolType()},
          {kHintIndexStrategy, zetasql::types::StringType()},
          {kHintAllowSearchIndexesInTransaction, zetasql::types::BoolType()},
          {kRequireEnhanceQuery, zetasql::types::BoolType()},
          {kEnhanceQueryTimeoutMs, zetasql::types::Int64Type()},
          {kScanMethod, zetasql::types::StringType()},
      }};

  const auto& iter = supported_hint_types->find(name);
  ZETASQL_RET_CHECK(iter != supported_hint_types->cend());
  if (!value.type()->Equals(iter->second)) {
    return error::InvalidHintValue(name, value.DebugString());
  }
  if (absl::EqualsIgnoreCase(name, kHintForceIndex)) {
    const std::string& index_name = value.string_value();
    bool base_table_hint = absl::EqualsIgnoreCase(index_name, kHintBaseTable);
    if (!base_table_hint) {
      // Statement-level FORCE_INDEX hints can only be '_BASE_TABLE'.
      if (node_kind == zetasql::RESOLVED_QUERY_STMT) {
        return error::InvalidStatementHintValue(name, value.DebugString());
      }
      const std::vector<const Index*> indexes =
          context_.schema->FindIndexesUnderName(index_name);
      if (indexes.empty()) {
        // We don't have the table name here. So this will not match prod error
        // message.
        return error::QueryHintIndexNotFound("", index_name);
      }
      for (const auto& index : indexes) {
        indexes_used_.insert(index);
      }
    }
  } else if (absl::EqualsIgnoreCase(name, kHintJoinMethod) ||
             absl::EqualsIgnoreCase(name, kHintJoinTypeDeprecated)) {
    const std::string& string_value = value.string_value();
    if (!(absl::EqualsIgnoreCase(string_value, kHintJoinTypeApply) ||
          absl::EqualsIgnoreCase(string_value, kHintJoinTypeHash) ||
          absl::EqualsIgnoreCase(string_value, kHintJoinTypeMerge) ||
          absl::EqualsIgnoreCase(string_value,
                                 kHintJoinTypePushBroadcastHashJoin) ||
          absl::EqualsIgnoreCase(string_value,
                                 kHintJoinTypeNestedLoopDeprecated))) {
      return error::InvalidHintValue(name, value.DebugString());
    }
  } else if (absl::EqualsIgnoreCase(name, kHintParameterSensitive)) {
    const std::string& string_value = value.string_value();
    if (!(absl::EqualsIgnoreCase(string_value, kHintParameterSensitiveAlways) ||
          absl::EqualsIgnoreCase(string_value, kHintParameterSensitiveAuto) ||
          absl::EqualsIgnoreCase(string_value, kHintParameterSensitiveNever))) {
      return error::InvalidHintValue(name, value.DebugString());
    }
  } else if (absl::EqualsIgnoreCase(name, kHashJoinBuildSide)) {
    bool is_hash_join = [&]() {
      auto it = hint_map.find(kHintJoinMethod);
      if (it != hint_map.end() &&
          absl::EqualsIgnoreCase(it->second.string_value(),
                                 kHintJoinTypeHash)) {
        return true;
      }
      it = hint_map.find(kHintJoinTypeDeprecated);
      if (it != hint_map.end() &&
          absl::EqualsIgnoreCase(it->second.string_value(),
                                 kHintJoinTypeHash)) {
        return true;
      }
      return false;
    }();
    if (!is_hash_join) {
      return error::InvalidHintForNode(kHashJoinBuildSide, "HASH joins");
    }
    const std::string& string_value = value.string_value();
    if (!(absl::EqualsIgnoreCase(string_value, kHashJoinBuildSideLeft) ||
          absl::EqualsIgnoreCase(string_value, kHashJoinBuildSideRight))) {
      return error::InvalidHintValue(name, value.DebugString());
    }
  } else if (absl::EqualsIgnoreCase(name, kHashJoinExecution)) {
    const std::string& string_value = value.string_value();
    if (!(absl::EqualsIgnoreCase(string_value, kHashJoinExecutionOnePass) ||
          absl::EqualsIgnoreCase(string_value, kHashJoinExecutionMultiPass))) {
      return error::InvalidHintValue(name, value.DebugString());
    }
  } else if (absl::EqualsIgnoreCase(name, kHintGroupMethod) ||
             absl::EqualsIgnoreCase(name, kHintGroupTypeDeprecated)) {
    const std::string& string_value = value.string_value();
    if (!(absl::EqualsIgnoreCase(string_value, kHintGroupMethodHash) ||
          absl::EqualsIgnoreCase(string_value, kHintGroupMethodStream))) {
      return error::InvalidHintValue(name, value.DebugString());
    }
  } else if (absl::EqualsIgnoreCase(name, kHintLockScannedRange)) {
    const std::string& string_value = value.string_value();
    if (!(absl::EqualsIgnoreCase(string_value,
                                 kHintLockScannedRangeExclusive) ||
          absl::EqualsIgnoreCase(string_value, kHintLockScannedRangeShared))) {
      return error::InvalidHintValue(name, value.DebugString());
    }
  } else if (absl::EqualsIgnoreCase(name, kHintIndexStrategy)) {
    const std::string& string_value = value.string_value();
    if (!absl::EqualsIgnoreCase(string_value,
                                kHintIndexStrategyForceIndexUnion)) {
      return error::InvalidHintValue(name, value.DebugString());
    }
  } else if (absl::EqualsIgnoreCase(name, kScanMethod)) {
    const std::string& string_value = value.string_value();
    if (!(absl::EqualsIgnoreCase(string_value, kScanMethodBatch) ||
          absl::EqualsIgnoreCase(string_value, kScanMethodRow))) {
      return error::InvalidHintValue(name, value.DebugString());
    }
  }
  return absl::OkStatus();
}

absl::Status QueryValidator::ExtractSpannerOptionsForNode(
    const absl::flat_hash_map<absl::string_view, zetasql::Value>&
        node_hint_map) {
  // Only extract options if they are requested.
  if (extracted_options_ == nullptr) {
    return absl::OkStatus();
  }

  for (const auto& [hint_name, hint_value] : node_hint_map) {
    if (absl::EqualsIgnoreCase(hint_name,
                               kHintAllowSearchIndexesInTransaction)) {
      // We already checked the hint type in CheckHintValue.
      extracted_options_->allow_search_indexes_in_transaction =
          hint_value.bool_value();
    }
    if (absl::EqualsIgnoreCase(hint_name, kHintLockScannedRange)) {
      // We already checked the hint type in CheckHintValue.
      query_features_.has_lock_scanned_ranges = true;
    }
  }
  return absl::OkStatus();
}

absl::Status QueryValidator::ExtractEmulatorOptionsForNode(
    const absl::flat_hash_map<absl::string_view, zetasql::Value>&
        node_hint_map) const {
  // Only extract options if they are requested.
  if (extracted_options_ == nullptr) return absl::OkStatus();

  for (const auto& [hint_name, hint_value] : node_hint_map) {
    if (absl::EqualsIgnoreCase(hint_name,
                               kHintDisableQueryPartitionabilityCheck)) {
      if (!hint_value.type()->IsBool()) {
        return error::InvalidEmulatorHintValue(hint_name,
                                               hint_value.DebugString());
      } else {
        extracted_options_->disable_query_partitionability_check =
            hint_value.bool_value();
      }
    }
    if (absl::EqualsIgnoreCase(hint_name,
                               kHintDisableQueryNullFilteredIndexCheck)) {
      if (!hint_value.type()->IsBool()) {
        return error::InvalidEmulatorHintValue(hint_name,
                                               hint_value.DebugString());
      } else {
        extracted_options_->disable_query_null_filtered_index_check =
            hint_value.bool_value();
      }
    }
  }
  return absl::OkStatus();
}

absl::Status QueryValidator::CheckTableScanLockModeAllowed(
    const QueryEngineOptions* options, const QueryContext& context) const {
  // FOR UPDATE can't be run in a read-only transaction.
  if (context.is_read_only_txn != std::nullopt &&
      context.is_read_only_txn.value()) {
    return error::ForUpdateUnsupportedInReadOnlyTransactions();
  }

  // FOR UPDATE can't be combined with lock_scanned_ranges hints.
  if (query_features_.has_lock_scanned_ranges) {
    return error::ForUpdateCannotCombineWithLockScannedRanges();
  }

  return absl::OkStatus();
}

namespace {

absl::Status CheckAllowedCasts(const zetasql::Type* from_type,
                               const zetasql::Type* to_type) {
  if (to_type->IsArray() && from_type->IsArray() &&
      !to_type->AsArray()->element_type()->Equals(
          from_type->AsArray()->element_type())) {
    return error::NoFeatureSupportDifferentTypeArrayCasts(
        from_type->DebugString(), to_type->DebugString());
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status QueryValidator::DefaultVisit(const zetasql::ResolvedNode* node) {
  ZETASQL_RETURN_IF_ERROR(ValidateHints(node));
  return zetasql::ResolvedASTVisitor::DefaultVisit(node);
}

absl::Status QueryValidator::VisitResolvedQueryStmt(
    const zetasql::ResolvedQueryStmt* node) {
  for (const auto& column : node->output_column_list()) {
    if (column->column().type()->IsStruct())
      return error::UnsupportedReturnStructAsColumn();
  }

  if (IsSelectForUpdateQuery(*node)) {
    query_features_.has_for_update = true;
  }

  ZETASQL_RETURN_IF_ERROR(DefaultVisit(node));

  if (IsSelectForUpdateQuery(*node)) {
    ZETASQL_RETURN_IF_ERROR(
        CheckTableScanLockModeAllowed(extracted_options_, context_));
  }

  return absl::OkStatus();
}

absl::Status QueryValidator::CheckSearchFunctionsAreAllowed(
    const zetasql::ResolvedFunctionCall& function_call) {
  static const auto* search_functions =
      new const absl::flat_hash_set<absl::string_view, zetasql_base::StringViewCaseHash,
                                    zetasql_base::StringViewCaseEqual>{
          "search", "search_substring", "score", "snippet"};

  const std::string name = function_call.function()->FullName(false);
  if (search_functions->find(name) != search_functions->end()) {
    if (in_partition_query_) {
      // Not allowed in batch query.
      return error::InvalidUseOfSearchRelatedFunctionWithReason(
          "SQL Search functions are not supported for partitioned queries");
    }
    if (!IsSearchQueryAllowed(extracted_options_, context_)) {
      return error::InvalidUseOfSearchRelatedFunctionWithReason(
          "SQL Search functions are not supported for transactional "
          "queries by default");
    };
    if (query_features_.has_for_update) {
      return error::ForUpdateUnsupportedInSearchQueries();
    }
  }

  return absl::OkStatus();
}

absl::Status QueryValidator::VisitResolvedLiteral(
    const zetasql::ResolvedLiteral* node) {
  if (node->type()->IsArray() &&
      node->type()->AsArray()->element_type()->IsStruct() &&
      node->value().is_empty_array()) {
    return error::UnsupportedArrayConstructorSyntaxForEmptyStructArray();
  }

  return DefaultVisit(node);
}

bool QueryValidator::IsReadWriteOnlyFunction(absl::string_view name) const {
  if (name == kGetNextSequenceValueFunctionName) {
    return true;
  }
  return false;
}

absl::Status QueryValidator::VisitResolvedFunctionCall(
    const zetasql::ResolvedFunctionCall* node) {
  // Check if function is part of supported subset of ZetaSQL
  if (node->function()->IsZetaSQLBuiltin()) {
    if (!IsSupportedZetaSQLFunction(*node->function())) {
      return error::UnsupportedFunction(node->function()->SQLName());
    }
  }

  // Out of the supported subset of ZetaSQL, filter out functions that
  // are unimplemented or may require additional validation of arguments.
  ZETASQL_RETURN_IF_ERROR(FilterSafeModeFunction(*node));
  ZETASQL_RETURN_IF_ERROR(
      FilterResolvedFunction(language_options_, sql_features_, *node));

  if (node->function()->FullName(/*include_group=*/false) == "cast") {
    ZETASQL_RETURN_IF_ERROR(
        CheckAllowedCasts(node->argument_list(0)->type(), node->type()));
  }

  ZETASQL_RETURN_IF_ERROR(CheckSearchFunctionsAreAllowed(*node));

  if (IsSequenceFunction(node)) {
    ZETASQL_RETURN_IF_ERROR(ValidateSequenceFunction(node));
  }

  if (!context_.allow_read_write_only_functions &&
      IsReadWriteOnlyFunction(
          node->function()->FullName(/*include_group=*/false))) {
    return error::ReadOnlyTransactionDoesNotSupportReadWriteOnlyFunctions(
        node->function()->FullName(/*include_group=*/false));
  }

  return DefaultVisit(node);
}

bool QueryValidator::IsSequenceFunction(
    const zetasql::ResolvedFunctionCall* node) const {
  return (node->function()->FullName(/*include_group=*/false) ==
              kGetNextSequenceValueFunctionName ||
          node->function()->FullName(/*include_group=*/false) ==
              kGetInternalSequenceStateFunctionName);
}

absl::Status QueryValidator::ValidateSequenceFunction(
    const zetasql::ResolvedFunctionCall* node) {
  if (schema()->dialect() ==
      database_api::DatabaseDialect::GOOGLE_STANDARD_SQL) {
    if (node->generic_argument_list_size() != 1 ||
        node->generic_argument_list(0)->sequence() == nullptr) {
      return error::NoMatchingFunctionSignature(
          node->function()->FullName(/*include_group=*/false), "SEQUENCE");
    }
    const std::string sequence_name =
        node->generic_argument_list(0)->sequence()->sequence()->FullName();
    const Sequence* current_sequence =
        schema()->FindSequence(sequence_name,
                               /*exclude_internal=*/true);
    if (current_sequence == nullptr) {
      return error::SequenceNotFound(sequence_name);
    }
    dependent_sequences_.insert(current_sequence);
    return absl::OkStatus();
  }

  // This is when the dialect is PostgreSQL.
  if (node->argument_list_size() == 1 &&
      node->argument_list(0)->node_kind() == zetasql::RESOLVED_LITERAL) {
    const zetasql::Value& value =
        node->argument_list(0)->GetAs<zetasql::ResolvedLiteral>()->value();
    if (value.type()->IsString()) {
      const Sequence* current_sequence =
          schema()->FindSequence(value.string_value(),
                                 /*exclude_internal=*/true);
      if (current_sequence == nullptr) {
        return error::SequenceNotFound(value.string_value());
      }
      dependent_sequences_.insert(current_sequence);
    }
  }
  return absl::OkStatus();
}

absl::Status QueryValidator::VisitResolvedAggregateFunctionCall(
    const zetasql::ResolvedAggregateFunctionCall* node) {
  // Check if function is part of supported subset of ZetaSQL
  if (node->function()->IsZetaSQLBuiltin()) {
    if (!IsSupportedZetaSQLFunction(*node->function())) {
      return error::UnsupportedFunction(node->function()->SQLName());
    }
  }

  // Out of the supported subset of ZetaSQL, filter out functions that
  // are unimplemented or may require additional validation of arguments.
  ZETASQL_RETURN_IF_ERROR(FilterSafeModeFunction(*node));
  ZETASQL_RETURN_IF_ERROR(FilterResolvedAggregateFunction(sql_features_, *node));
  return DefaultVisit(node);
}

absl::Status QueryValidator::VisitResolvedCast(
    const zetasql::ResolvedCast* node) {
  ZETASQL_RETURN_IF_ERROR(CheckAllowedCasts(node->expr()->type(), node->type()));
  return DefaultVisit(node);
}

absl::Status QueryValidator::VisitResolvedSampleScan(
    const zetasql::ResolvedSampleScan* node) {
  if (node->repeatable_argument()) {
    return error::UnsupportedTablesampleRepeatable();
  }

  if (absl::EqualsIgnoreCase(node->method(), "system")) {
    return error::UnsupportedTablesampleSystem();
  }
  return DefaultVisit(node);
}

absl::Status QueryValidator::CheckPendingCommitTimestampReads(
    const zetasql::ResolvedTableScan* table_scan,
    absl::Span<const zetasql::ResolvedStatement::ObjectAccess> access_list) {
  ZETASQL_RET_CHECK(access_list.empty() ||
            access_list.size() == table_scan->column_index_list_size());
  // A commit timestamp tracker is not always present (e.g. read-only txns).
  if (context_.commit_timestamp_tracker == nullptr) {
    return absl::OkStatus();
  }

  // Any table in the user schema will be a QueryableTable. We use this property
  // to skip table scans against system tables (e.g. information_schema.tables)
  // since these tables do not have corresponding backend schema nodes.
  //
  // Skipping these tables is safe because they are not writable and do not
  // contain commit timestamps (pending or otherwise).
  if (!table_scan->table()->Is<QueryableTable>()) {
    return absl::OkStatus();
  }

  const Table* table =
      table_scan->table()->GetAs<QueryableTable>()->wrapped_table();
  ZETASQL_RET_CHECK(table != nullptr);
  std::vector<const Column*> columns;
  for (int i = 0; i < table_scan->column_index_list_size(); ++i) {
    // Ignore scan columns which are not read
    if (i < access_list.size() &&
        !(access_list[i] & zetasql::ResolvedStatement::READ)) {
      continue;
    }
    int idx = table_scan->column_index_list(i);
    std::string column_name = table_scan->table()->GetColumn(idx)->Name();
    const Column* column = table->FindColumn(column_name);
    ZETASQL_RET_CHECK(column != nullptr);
    columns.push_back(column);
  }
  return context_.commit_timestamp_tracker->CheckRead(table, columns);
}

absl::Status QueryValidator::VisitResolvedInsertStmt(
    const zetasql::ResolvedInsertStmt* node) {
  if (node->table_scan() != nullptr) {
    dml_table_scans_.insert(node->table_scan());
  }
  return DefaultVisit(node);
}

absl::Status QueryValidator::VisitResolvedUpdateStmt(
    const zetasql::ResolvedUpdateStmt* node) {
  if (node->table_scan() != nullptr) {
    ZETASQL_RETURN_IF_ERROR(CheckPendingCommitTimestampReads(
        node->table_scan(), node->column_access_list()));
    dml_table_scans_.insert(node->table_scan());
  }
  return DefaultVisit(node);
}

absl::Status QueryValidator::VisitResolvedDeleteStmt(
    const zetasql::ResolvedDeleteStmt* node) {
  if (node->table_scan() != nullptr) {
    ZETASQL_RETURN_IF_ERROR(CheckPendingCommitTimestampReads(
        node->table_scan(), node->column_access_list()));
    dml_table_scans_.insert(node->table_scan());
  }
  return DefaultVisit(node);
}

absl::Status QueryValidator::VisitResolvedTableScan(
    const zetasql::ResolvedTableScan* node) {
  // Skip table scans owned by DML statements. These have already been handled.
  if (!dml_table_scans_.contains(node)) {
    ZETASQL_RETURN_IF_ERROR(CheckPendingCommitTimestampReads(node));
  }

  return DefaultVisit(node);
}

}  // namespace backend
}  // namespace emulator
}  // namespace spanner
}  // namespace google
