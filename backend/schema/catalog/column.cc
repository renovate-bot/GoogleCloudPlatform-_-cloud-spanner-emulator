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

#include "backend/schema/catalog/column.h"

#include <algorithm>
#include <string>

#include "zetasql/public/options.pb.h"
#include "zetasql/public/type.pb.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "backend/schema/catalog/locality_group.h"
#include "backend/schema/catalog/table.h"
#include "backend/schema/graph/schema_graph_editor.h"
#include "backend/schema/graph/schema_node.h"
#include "backend/schema/updater/schema_validation_context.h"
#include "zetasql/base/ret_check.h"
#include "absl/status/status.h"
#include "zetasql/base/status_macros.h"

namespace google {
namespace spanner {
namespace emulator {
namespace backend {

namespace {

bool IsNullFilteredIndexColumn(const Column* column) {
  // The column should be filtered if:
  // 1. The index is NULL_FILTERED and the column is part of the index key.
  // 2. The column is specified in the WHERE IS NOT NULL clause.
  bool is_null_filtered_index_key = false;
  if (column->table()->owner_index()->is_null_filtered()) {
    const auto& index_key = column->table()->owner_index()->key_columns();
    auto it = std::find_if(index_key.begin(), index_key.end(),
                           [column](const KeyColumn* key_column) {
                             return key_column->column()->id() == column->id();
                           });
    is_null_filtered_index_key = (it != index_key.end());
  }
  bool is_not_null_column =
      column->table()->owner_index()->is_null_filtered_column(column);
  return is_null_filtered_index_key || is_not_null_column;
}

}  // namespace

const ChangeStream* Column::FindChangeStream(
    const std::string& change_stream_name) const {
  auto itr = std::find_if(change_streams_.begin(), change_streams_.end(),
                          [&change_stream_name](const auto& change_stream) {
                            return absl::EqualsIgnoreCase(change_stream->Name(),
                                                          change_stream_name);
                          });
  if (itr == change_streams_.end()) {
    return nullptr;
  }
  return *itr;
}

std::string Column::FullName() const {
  return absl::StrCat(table_->Name(), ".", name_);
}

absl::Status Column::Validate(SchemaValidationContext* context) const {
  return validate_(this, context);
}

absl::Status Column::ValidateUpdate(const SchemaNode* orig,
                                    SchemaValidationContext* context) const {
  return validate_update_(this, orig->As<const Column>(), context);
}

absl::Status Column::DeepClone(SchemaGraphEditor* editor,
                               const SchemaNode* orig) {
  ZETASQL_ASSIGN_OR_RETURN(const auto* table_clone, editor->Clone(table_));
  table_ = table_clone->As<const Table>();
  // The column should be deleted if the table containing the column
  // is deleted.
  if (table_->is_deleted()) {
    MarkDeleted();
  }

  ZETASQL_RETURN_IF_ERROR(editor->CloneVector(&change_streams_));
  ZETASQL_RETURN_IF_ERROR(
      editor->CloneVector(&change_streams_explicitly_tracking_column_));

  if (locality_group_) {
    ZETASQL_ASSIGN_OR_RETURN(const auto* locality_group_clone,
                     editor->Clone(locality_group_));
    locality_group_ = locality_group_clone->As<const LocalityGroup>();
  }

  for (const Column*& column : dependent_columns_) {
    ZETASQL_ASSIGN_OR_RETURN(const auto* schema_node, editor->Clone(column));
    column = schema_node->As<const Column>();
  }
  for (const SchemaNode*& dependency : sequences_used_) {
    ZETASQL_ASSIGN_OR_RETURN(const SchemaNode* cloned_dep, editor->Clone(dependency));
    dependency = cloned_dep;
  }
  for (const SchemaNode*& dependency : udf_dependencies_) {
    ZETASQL_ASSIGN_OR_RETURN(const SchemaNode* cloned_dep, editor->Clone(dependency));
    dependency = cloned_dep;
  }

  if (source_column_) {
    ZETASQL_ASSIGN_OR_RETURN(const auto* source_column_clone,
                     editor->Clone(source_column_));
    source_column_ = source_column_clone->As<const Column>();
    // Source column's type attributes must be copied explicitly as they
    // may have been changed by an ALTER on the source column.
    // However, nullability should not be copied if the column is part of
    // the null-filtering index.
    type_ = source_column_->type_;
    declared_max_length_ = source_column_->declared_max_length_;
    ZETASQL_RET_CHECK(!table_->is_public());
    // is_nullable should be false for null-filtered index columns, otherwise
    // it should be the same as the source column.
    is_nullable_ =
        IsNullFilteredIndexColumn(this) ? false : source_column_->is_nullable_;
  }
  return absl::OkStatus();
}

void Column::PopulateDependentColumns() {
  dependent_columns_.clear();
  for (absl::string_view dep_name : dependent_column_names_) {
    if (absl::EqualsIgnoreCase(dep_name, name_)) {
      dependent_columns_.push_back(this);
    } else {
      dependent_columns_.push_back(table_->FindColumn(std::string(dep_name)));
    }
  }
}

absl::Status KeyColumn::Validate(SchemaValidationContext* context) const {
  return validate_(this, context);
}

absl::Status KeyColumn::ValidateUpdate(const SchemaNode* orig,
                                       SchemaValidationContext* context) const {
  return validate_update_(this, orig->As<const KeyColumn>(), context);
}

absl::Status KeyColumn::DeepClone(SchemaGraphEditor* editor,
                                  const SchemaNode* orig) {
  ZETASQL_ASSIGN_OR_RETURN(const auto* cloned_column, editor->Clone(column_));
  column_ = cloned_column->As<const Column>();
  if (column_->is_deleted()) {
    MarkDeleted();
  }
  return absl::OkStatus();
}

}  // namespace backend
}  // namespace emulator
}  // namespace spanner
}  // namespace google
