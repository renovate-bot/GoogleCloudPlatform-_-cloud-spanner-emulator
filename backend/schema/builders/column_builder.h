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

#ifndef THIRD_PARTY_CLOUD_SPANNER_EMULATOR_BACKEND_SCHEMA_BUILDERS_COLUMN_BUILDER_H
#define THIRD_PARTY_CLOUD_SPANNER_EMULATOR_BACKEND_SCHEMA_BUILDERS_COLUMN_BUILDER_H

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "zetasql/public/type.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/memory/memory.h"
#include "absl/strings/match.h"
#include "backend/common/ids.h"
#include "backend/schema/catalog/change_stream.h"
#include "backend/schema/catalog/column.h"
#include "backend/schema/catalog/locality_group.h"
#include "backend/schema/graph/schema_node.h"
#include "backend/schema/validators/column_validator.h"

namespace google {
namespace spanner {
namespace emulator {
namespace backend {

class Table;

class Column::Builder {
 public:
  Builder()
      : instance_(absl::WrapUnique(new Column(
            ColumnValidator::Validate, ColumnValidator::ValidateUpdate))) {}

  std::unique_ptr<const Column> build() { return std::move(instance_); }

  const Column* get() const { return instance_.get(); }

  Builder& set_id(const ColumnID id) {
    instance_->id_ = id;
    return *this;
  }

  Builder& set_name(const std::string& name) {
    instance_->name_ = name;
    return *this;
  }

  Builder& set_type(const zetasql::Type* type) {
    instance_->type_ = type;
    return *this;
  }

  Builder& set_nullable(bool is_nullable) {
    instance_->is_nullable_ = is_nullable;
    return *this;
  }

  Builder& set_is_placement_key(bool is_placement_key) {
    instance_->is_placement_key_ = is_placement_key;
    return *this;
  }

  Builder& set_expression(const std::string& expression) {
    instance_->expression_ = expression;
    return *this;
  }

  Builder& set_original_expression(const std::string& expression) {
    instance_->original_expression_ = expression;
    return *this;
  }

  Builder& set_has_default_value(bool has_default_value) {
    instance_->has_default_value_ = has_default_value;
    return *this;
  }

  Builder& set_is_identity_column(bool is_identity_column) {
    instance_->is_identity_column_ = is_identity_column;
    return *this;
  }

  Builder& clear_expression() {
    instance_->expression_.reset();
    return *this;
  }

  Builder& clear_original_expression() {
    instance_->original_expression_.reset();
    return *this;
  }

  Builder& add_dependent_column_name(const std::string& column_name) {
    instance_->dependent_column_names_.push_back(column_name);
    return *this;
  }

  Builder& set_declared_max_length(std::optional<int64_t> length) {
    instance_->declared_max_length_ = length;
    return *this;
  }

  Builder& set_vector_length(std::optional<int32_t> vector_length) {
    instance_->vector_length_ = vector_length;
    return *this;
  }

  Builder& set_table(const Table* table) {
    instance_->table_ = table;
    return *this;
  }

  Builder& set_allow_commit_timestamp(std::optional<bool> allow) {
    instance_->allows_commit_timestamp_ = allow;
    return *this;
  }

  Builder& set_source_column(const Column* column) {
    instance_->source_column_ = column;
    instance_->type_ = column->type_;
    instance_->declared_max_length_ = column->declared_max_length_;
    instance_->vector_length_ = column->vector_length_;
    return *this;
  }

  Builder& set_hidden(bool hidden) {
    instance_->hidden_ = hidden;
    return *this;
  }

  Builder& set_sequences_used(
      const absl::flat_hash_set<const SchemaNode*>& sequences_used) {
    instance_->sequences_used_.clear();
    instance_->sequences_used_.reserve(sequences_used.size());
    for (const SchemaNode* node : sequences_used) {
      instance_->sequences_used_.push_back(node);
    }
    return *this;
  }

  Builder& set_udf_dependencies(
      const absl::flat_hash_set<const SchemaNode*>& udf_dependencies) {
    instance_->udf_dependencies_.clear();
    instance_->udf_dependencies_.reserve(udf_dependencies.size());
    for (const SchemaNode* node : udf_dependencies) {
      instance_->udf_dependencies_.push_back(node);
    }
    return *this;
  }

  Builder& set_stored(bool stored) {
    instance_->is_stored_ = stored;
    return *this;
  }

  Builder& set_postgresql_oid(std::optional<uint32_t> postgresql_oid) {
    if (postgresql_oid.has_value()) {
      instance_->set_postgresql_oid(postgresql_oid.value());
    }
    return *this;
  }

  Builder& set_locality_group(const LocalityGroup* locality_group) {
    instance_->locality_group_ = locality_group;
    return *this;
  }

 private:
  std::unique_ptr<Column> instance_;
};

class Column::Editor {
 public:
  explicit Editor(Column* instance) : instance_(instance) {}

  const Column* get() const { return instance_; }

  Editor& set_type(const zetasql::Type* type) {
    instance_->type_ = type;
    return *this;
  }

  Editor& set_nullable(bool is_nullable) {
    instance_->is_nullable_ = is_nullable;
    return *this;
  }

  Editor& set_expression(const std::string& expression) {
    instance_->expression_ = expression;
    return *this;
  }

  Editor& set_original_expression(const std::string& expression) {
    instance_->original_expression_ = expression;
    return *this;
  }

  Editor& set_has_default_value(bool has_default_value) {
    instance_->has_default_value_ = has_default_value;
    return *this;
  }

  Editor& set_is_identity_column(bool is_identity_column) {
    instance_->is_identity_column_ = is_identity_column;
    return *this;
  }

  Editor& clear_expression() {
    instance_->expression_.reset();
    return *this;
  }

  Editor& add_change_stream(const ChangeStream* change_stream) {
    instance_->change_streams_.push_back(change_stream);
    return *this;
  }

  Editor& add_change_stream_explicitly_tracking_column(
      const ChangeStream* change_stream) {
    instance_->change_streams_explicitly_tracking_column_.push_back(
        change_stream);
    return *this;
  }

  Editor& remove_change_stream(const ChangeStream* change_stream) {
    auto itr = std::find_if(
        instance_->change_streams_.begin(), instance_->change_streams_.end(),
        [change_stream](const auto& change_stream_element) {
          return absl::EqualsIgnoreCase(change_stream->Name(),
                                        change_stream_element->Name());
        });
    ABSL_DCHECK(itr != instance_->change_streams_.end());
    instance_->change_streams_.erase(itr);

    // Remove the change stream from the list of change streams explicitly
    // tracking the column if exists.
    auto itr_tracking_columns = std::find_if(
        instance_->change_streams_explicitly_tracking_column_.begin(),
        instance_->change_streams_explicitly_tracking_column_.end(),
        [change_stream](const auto& change_stream_element) {
          return absl::EqualsIgnoreCase(change_stream->Name(),
                                        change_stream_element->Name());
        });
    if (itr_tracking_columns !=
        instance_->change_streams_explicitly_tracking_column_.end()) {
      instance_->change_streams_explicitly_tracking_column_.erase(
          itr_tracking_columns);
    }
    return *this;
  }

  Editor& set_locality_group(const LocalityGroup* locality_group) {
    instance_->locality_group_ = locality_group;
    return *this;
  }

  Editor& clear_locality_group() {
    instance_->locality_group_ = nullptr;
    return *this;
  }

  Editor& clear_original_expression() {
    instance_->original_expression_.reset();
    return *this;
  }

  Editor& add_dependent_column_name(const std::string& column_name) {
    instance_->dependent_column_names_.push_back(column_name);
    return *this;
  }

  Editor& set_declared_max_length(std::optional<int64_t> length) {
    instance_->declared_max_length_ = length;
    return *this;
  }

  Editor& set_vector_length(std::optional<int32_t> vector_length) {
    instance_->vector_length_ = vector_length;
    return *this;
  }

  Editor& set_allow_commit_timestamp(std::optional<bool> allow) {
    instance_->allows_commit_timestamp_ = allow;
    return *this;
  }

  Editor& set_sequences_used(
      const absl::flat_hash_set<const SchemaNode*>& sequences_used) {
    instance_->sequences_used_.clear();
    instance_->sequences_used_.reserve(sequences_used.size());
    for (const SchemaNode* node : sequences_used) {
      instance_->sequences_used_.push_back(node);
    }
    return *this;
  }

  Editor& set_udf_dependencies(
      const absl::flat_hash_set<const SchemaNode*>& udf_dependencies) {
    instance_->udf_dependencies_.clear();
    instance_->udf_dependencies_.reserve(udf_dependencies.size());
    for (const SchemaNode* udf : udf_dependencies) {
      instance_->udf_dependencies_.push_back(udf);
    }
    return *this;
  }

  Editor& set_stored(bool stored) {
    instance_->is_stored_ = stored;
    return *this;
  }

  Editor& set_postgresql_oid(std::optional<uint32_t> postgresql_oid) {
    if (postgresql_oid.has_value()) {
      instance_->set_postgresql_oid(postgresql_oid.value());
    }
    return *this;
  }

 private:
  // Not owned.
  Column* instance_;
};

class KeyColumn::Builder {
 public:
  Builder()
      : instance_(absl::WrapUnique(
            new KeyColumn(KeyColumnValidator::Validate,
                          KeyColumnValidator::ValidateUpdate))) {}

  std::unique_ptr<const KeyColumn> build() { return std::move(instance_); }

  const KeyColumn* get() const { return instance_.get(); }

  Builder& set_column(const Column* column) {
    instance_->column_ = column;
    return *this;
  }

  Builder& set_descending(bool desceding) {
    instance_->is_descending_ = desceding;
    return *this;
  }
  Builder& set_nulls_last(bool nulls_last) {
    instance_->is_nulls_last_ = nulls_last;
    return *this;
  }

  Builder& set_postgresql_oid(std::optional<uint32_t> postgresql_oid) {
    if (postgresql_oid.has_value()) {
      instance_->set_postgresql_oid(postgresql_oid.value());
    }
    return *this;
  }

 private:
  std::unique_ptr<KeyColumn> instance_;
};

}  // namespace backend
}  // namespace emulator
}  // namespace spanner
}  // namespace google

#endif  // THIRD_PARTY_CLOUD_SPANNER_EMULATOR_BACKEND_SCHEMA_BUILDERS_COLUMN_BUILDER_H
