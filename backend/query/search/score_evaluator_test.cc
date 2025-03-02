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

#include "backend/query/search/score_evaluator.h"

#include <string>
#include <vector>

#include "zetasql/public/value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "zetasql/base/testing/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "backend/query/search/exact_match_tokenizer.h"
#include "backend/query/search/tokenizer.h"

namespace google {
namespace spanner {
namespace emulator {
namespace backend {
namespace query {
namespace search {

using testing::HasSubstr;
using zetasql_base::testing::StatusIs;

TEST(ScoreEvaluatorTest, EvaluateWrongSearchColumnType) {
  std::vector<zetasql::Value> args;
  args.push_back(zetasql::Value::Bool(false));
  args.push_back(zetasql::Value::String("test"));

  EXPECT_THAT(
      ScoreEvaluator::Evaluate(args),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Invalid search query. Trying to execute search related "
                    "function on unsupported column type: BOOL.")));
}

TEST(ScoreEvaluatorTest, EvaluateWrongSearchQueryType) {
  std::vector<zetasql::Value> args;
  args.push_back(zetasql::Value::NullTokenList());
  args.push_back(zetasql::Value::Bool(false));

  EXPECT_THAT(ScoreEvaluator::Evaluate(args),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid search query type: BOOL.")));
}

TEST(ScoreEvaluatorTest, EvaluateOnWrongTokenList) {
  std::vector<zetasql::Value> args;
  const auto fulltext_tokenlist =
      ExactMatchTokenizer::Tokenize({zetasql::Value::String("numeric")});
  args.push_back(*fulltext_tokenlist);
  args.push_back(zetasql::Value::String("test"));

  EXPECT_THAT(
      ScoreEvaluator::Evaluate(args),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("SCORE function's first argument must be a TOKENLIST "
                         "column generated by TOKENIZE_FULLTEXT")));
}

struct ScoreEvaluatorTestCase {
  std::string original_doc;
  std::string query;
  double expected_result;
};

using ScoreEvaluatorTest = ::testing::TestWithParam<ScoreEvaluatorTestCase>;

TEST_P(ScoreEvaluatorTest, TestEvaluation) {
  const ScoreEvaluatorTestCase& test_case = GetParam();

  // Construct full text tokenized TokenList.
  std::vector<std::string> tokens =
      absl::StrSplit(test_case.original_doc, ", ");
  tokens.insert(tokens.begin(), "fulltext-0");
  zetasql::Value tokenlist = TokenListFromStrings(tokens);

  std::vector<zetasql::Value> args;
  args.push_back(tokenlist);
  args.push_back(zetasql::Value::String(test_case.query));

  absl::StatusOr<zetasql::Value> result = ScoreEvaluator::Evaluate(args);
  ZETASQL_EXPECT_OK(result.status());

  zetasql::Value score = result.value();
  EXPECT_TRUE(score.type()->IsDouble());

  EXPECT_EQ(test_case.expected_result, score.double_value());
}

INSTANTIATE_TEST_SUITE_P(
    ScoreEvaluatorTest, ScoreEvaluatorTest,
    testing::ValuesIn<ScoreEvaluatorTestCase>(
        {{"", "foo", 0.0},
         {"foo", "", 0.0},
         {"foo", "foo", 1.0},
         {"foo, foo, bar, foo, baz, foo, blah", "foo", 4.0},
         {"foo, foo, bar, foo, baz, foo, blah", "foo bar", 5.0},
         {"foo, foo, bar, foo, baz, foo, blah", "baz | blah", 2.0}}));

}  // namespace search
}  // namespace query
}  // namespace backend
}  // namespace emulator
}  // namespace spanner
}  // namespace google
