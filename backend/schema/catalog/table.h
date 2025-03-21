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

#ifndef THIRD_PARTY_CLOUD_SPANNER_EMULATOR_BACKEND_SCHEMA_CATALOG_TABLE_H_
#define THIRD_PARTY_CLOUD_SPANNER_EMULATOR_BACKEND_SCHEMA_CATALOG_TABLE_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "backend/common/case.h"
#include "backend/common/ids.h"
#include "backend/schema/catalog/change_stream.h"
#include "backend/schema/catalog/check_constraint.h"
#include "backend/schema/catalog/column.h"
#include "backend/schema/catalog/index.h"
#include "backend/schema/catalog/locality_group.h"
#include "backend/schema/ddl/operations.pb.h"
#include "backend/schema/graph/schema_graph_editor.h"
#include "backend/schema/graph/schema_node.h"

namespace google {
namespace spanner {
namespace emulator {
namespace backend {

class ForeignKey;
class Index;
class ChangeStream;

// Table represents a table in a database.
class Table : public SchemaNode {
 public:
  // Describes what action to take when the row from the parent table is
  // deleted.
  enum class OnDeleteAction {
    // Ensures that no child rows exist.
    kNoAction,

    // Delete the child rows of the parent row.
    kCascade
  };

  // Returns the name of the table.
  std::string Name() const { return name_; }

  // Returns the unique ID of this table.
  const TableID id() const { return id_; }

  // Returns the child tables of this table.
  absl::Span<const Table* const> children() const { return child_tables_; }

  // Returns the parent table of this table, or nullptr if this table does not
  // have a parent table.
  const Table* parent() const { return parent_table_; }

  // Returns the Change Stream that owns this table if one exists.
  const ChangeStream* owner_change_stream() const {
    return owner_change_stream_;
  }

  // Returns the locality group this column belongs to.
  const LocalityGroup* locality_group() const { return locality_group_; }

  // Returns the Index that owns this table, or nullptr if this table is not
  // owned by an Index.
  const Index* owner_index() const { return owner_index_; }

  // Returns the on delete action of this table.
  OnDeleteAction on_delete_action() const {
    return on_delete_action_.value_or(OnDeleteAction::kNoAction);
  }

  // Returns the row deletion policy of this table.
  std::optional<ddl::RowDeletionPolicy> row_deletion_policy() const {
    return row_deletion_policy_;
  }

  // Returns the list of all columns of this table.
  absl::Span<const Column* const> columns() const { return columns_; }

  // Returns the list of all foreign keys of this table.
  absl::Span<const ForeignKey* const> foreign_keys() const {
    return foreign_keys_;
  }

  // Returns the list of all check constraints of this table.
  absl::Span<const CheckConstraint* const> check_constraints() const {
    return check_constraints_;
  }

  // Returns the list of all foreign keys that are referencing this table.
  absl::Span<const ForeignKey* const> referencing_foreign_keys() const {
    return referencing_foreign_keys_;
  }

  // Returns the list of all indexes on this table.
  absl::Span<const Index* const> indexes() const { return indexes_; }

  // Returns the list of change streams tracking this entire table (explicitly
  // by table name or implicitly by ALL).
  absl::Span<const ChangeStream* const> change_streams() const {
    return change_streams_;
  }

  // Returns the list of all change streams explicitly tracking this table by
  // the table name.
  absl::Span<const ChangeStream* const>
  change_streams_explicitly_tracking_table() const {
    return change_streams_explicitly_tracking_table_;
  }

  bool is_trackable_by_change_stream() const { return is_public(); }

  std::vector<std::string> trackable_columns() const {
    std::vector<std::string> trackable_columns;
    for (const Column* column : columns_) {
      if (column->is_trackable_by_change_stream()) {
        trackable_columns.push_back(column->Name());
      }
    }
    return trackable_columns;
  }

  // Returns the primary key of this table.
  const absl::Span<const KeyColumn* const> primary_key() const {
    return primary_key_;
  }

  // Returns true if the Table is publicly visible, i.e. can be accessed
  // directly by a user request.
  bool is_public() const {
    return owner_change_stream_ == nullptr && owner_index_ == nullptr;
  }

  std::optional<uint32_t> interleave_in_parent_postgresql_oid() const {
    return interleave_in_parent_postgresql_oid_;
  }

  void set_interleave_in_parent_postgresql_oid(uint32_t oid) {
    interleave_in_parent_postgresql_oid_ = oid;
  }

  std::optional<uint32_t> primary_key_index_postgresql_oid() const {
    return primary_key_index_postgresql_oid_;
  }

  void set_primary_key_index_postgresql_oid(uint32_t oid) {
    primary_key_index_postgresql_oid_ = oid;
  }

  // Returns the synonym of this table.
  const std::string& synonym() const { return synonym_; }

  // Finds a column by its name. Returns a const pointer to the column, or
  // nullptr if the column is not found. Name comparison is case-insensitive.
  const Column* FindColumn(const std::string& column_name) const;

  // Finds an index on the table by its name. Name comparison is
  // case-insensitive.
  const Index* FindIndex(const std::string& index_name) const;

  // Finds an indexes fully qualified name given a name (qualified or not).
  std::string FindIndexQualifiedName(
      const std::string& qualified_or_unqualified_name) const;

  // Finds a change stream on the table by its name. Name comparison is
  // case-insensitive.
  const ChangeStream* FindChangeStream(
      const std::string& change_stream_name) const;

  // Same as above, but name comparison is case-sensitive.
  const Column* FindColumnCaseSensitive(const std::string& column_name) const;

  // Finds a KeyColumn by name. Returns nullptr if table doesn't contain
  // a column named `column_name` or if it's not a key column.
  const KeyColumn* FindKeyColumn(const std::string& column_name) const;

  // Returns the check constraint with a given constraint name. Returns nullptr
  // if not found.
  const CheckConstraint* FindCheckConstraint(
      const std::string& constraint_name) const;

  // Returns the foreign key with a given constraint name. Returns nullptr if
  // not found.
  const ForeignKey* FindForeignKey(const std::string& constraint_name) const;

  // Returns the foreign key with a given constraint name that references this
  // table. Returns nullptr if not found.
  const ForeignKey* FindReferencingForeignKey(
      const std::string& constraint_name) const;

  // Prints a debug string for the primary key in the following format:
  // <key_col1>, <key_col2>, ..., <key_coln>
  std::string PrimaryKeyDebugString() const;

  // SchemaNode interface implementation.
  // ------------------------------------

  std::optional<SchemaNameInfo> GetSchemaNameInfo() const override {
    return SchemaNameInfo{.name = name_, .kind = "Table", .global = true};
  }

  absl::Status Validate(SchemaValidationContext* context) const override;

  absl::Status ValidateUpdate(const SchemaNode* orig,
                              SchemaValidationContext* context) const override;

  std::string DebugString() const override {
    return absl::Substitute("T:$0[$1]", Name(), id_);
  }

  class Builder;
  class Editor;

 private:
  friend class TableValidator;

  using ValidationFn =
      std::function<absl::Status(const Table*, SchemaValidationContext*)>;
  using UpdateValidationFn = std::function<absl::Status(
      const Table*, const Table*, SchemaValidationContext*)>;

  // Constructors are private and only friend classes are able to build /
  // modify.
  Table(const ValidationFn& validate, const UpdateValidationFn& validate_update)
      : validate_(validate), validate_update_(validate_update) {}
  Table(const Table&) = default;

  std::unique_ptr<SchemaNode> ShallowClone() const override {
    auto clone = absl::WrapUnique(new Table(*this));
    return clone;
  }

  absl::Status DeepClone(SchemaGraphEditor* editor,
                         const SchemaNode* orig) override;

  // Validation delegates.
  const ValidationFn validate_;

  const UpdateValidationFn validate_update_;

  // The name of this table.
  std::string name_;

  // A unique ID for identifying this table in the schema that owns this table.
  TableID id_;

  // List of table columns, in the same order as found in the corresponding DDL.
  std::vector<const Column*> columns_;

  // List of check constraints defined on this table, in the same order as found
  // in the corresponding DDL.
  std::vector<const CheckConstraint*> check_constraints_;

  // List of foreign keys defined on this table, in the same order as found in
  // the corresponding DDL.
  std::vector<const ForeignKey*> foreign_keys_;

  // List of foreign keys referencing this table, in the same order as added in
  // the corresponding DDL.
  std::vector<const ForeignKey*> referencing_foreign_keys_;

  // List of indexes referring to this table. These are owned by the Schema, not
  // by the Table.
  std::vector<const Index*> indexes_;

  // List of change streams tracking this entire table (explicitly by table name
  // or implicitly by ALL). These are owned by the Schema, not by the Table.
  std::vector<const ChangeStream*> change_streams_;

  // List of change streams explicitly tracking this table by the table name.
  // These are owned by the Schema, not by the Table.
  std::vector<const ChangeStream*> change_streams_explicitly_tracking_table_;

  // The Change Stream that owns this table if one exists.
  const ChangeStream* owner_change_stream_ = nullptr;

  // The Index that owns this table if one exists. This attribute is null for
  // all but index data tables.
  const Index* owner_index_ = nullptr;

  // A map of case-insensitive column names to their backend::Column* pointers.
  CaseInsensitiveStringMap<const Column*> columns_map_;

  // primary_key_ defines the primary key columns of a table. Order of the
  // elements in the vector is the same as the order in which they are listed
  // in the PRIMARY KEY clause.
  // If 'this' represents the data table for an index, then primary_key_
  // consists of the owning index's key columns, followed by the indexed table's
  // key columns (excluding the columns already specified in the index key)
  // in the order defined in their respective CREATE INDEX ON and PRIMARY KEY
  // clauses.
  std::vector<const KeyColumn*> primary_key_;

  // Child tables that are interleaved in this table.
  std::vector<const Table*> child_tables_;

  // Parent table of this table. If null, this table is a top level table.
  const Table* parent_table_ = nullptr;

  // Action to take for a child row in 'this' table if a row from the parent
  // table is deleted. Set to nullopt if no action was specified by the user
  // in the CREATE TABLE statement.
  std::optional<OnDeleteAction> on_delete_action_ = std::nullopt;

  // Row deletion policy of this table. Set to nullopt if not specified.
  std::optional<ddl::RowDeletionPolicy> row_deletion_policy_ = std::nullopt;

  // Synonym.
  std::string synonym_;

  // Additional PG oids.
  std::optional<uint32_t> interleave_in_parent_postgresql_oid_ = std::nullopt;
  std::optional<uint32_t> primary_key_index_postgresql_oid_ = std::nullopt;

  // The locality group this table belongs to.
  const LocalityGroup* locality_group_ = nullptr;
};

// Returns the name of the schema declared owning object (index or table) of
// 'table'. Used to generate proper error messages.
std::string OwningObjectName(const Table* table);

// Returns the kind of object ("Index" or "Table") which is the owner of
// 'table'. Used to generate proper error messages.
std::string OwningObjectType(const Table* table);

}  // namespace backend
}  // namespace emulator
}  // namespace spanner
}  // namespace google
#endif  // THIRD_PARTY_CLOUD_SPANNER_EMULATOR_BACKEND_SCHEMA_CATALOG_TABLE_H_
