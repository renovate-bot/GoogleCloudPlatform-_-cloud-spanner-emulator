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

#include "backend/query/search/score_ngrams_evaluator.h"

#include <string>
#include <vector>

#include "zetasql/public/value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "zetasql/base/testing/status_matchers.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "backend/query/search/plain_full_text_tokenizer.h"
#include "backend/query/search/ngrams_tokenizer.h"
#include "backend/query/search/tokenizer.h"

namespace google {
namespace spanner {
namespace emulator {
namespace backend {
namespace query {
namespace search {

using testing::HasSubstr;
using zetasql_base::testing::StatusIs;

TEST(ScoreNgramsEvaluatorTest, EvaluateWrongSearchColumnType) {
  std::vector<zetasql::Value> args;
  args.push_back(zetasql::Value::Bool(false));
  args.push_back(zetasql::Value::String("test"));

  EXPECT_THAT(
      ScoreNgramsEvaluator::Evaluate(args),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Invalid search query. Trying to execute search related "
                    "function on unsupported column type: BOOL.")));
}

TEST(ScoreNgramsEvaluatorTest, EvaluateWrongSearchQueryType) {
  std::vector<zetasql::Value> args;
  const auto ngrams_tokenlist =
      NgramsTokenizer::Tokenize({zetasql::Value::String("foo")});
  args.push_back(*ngrams_tokenlist);
  args.push_back(zetasql::Value::Bool(false));

  EXPECT_THAT(ScoreNgramsEvaluator::Evaluate(args),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid search query type: BOOL.")));
}

TEST(ScoreNgramsEvaluatorTest, EvaluateOnWrongTokenList) {
  std::vector<zetasql::Value> args;
  const auto fulltext_tokenlist =
      PlainFullTextTokenizer::Tokenize({zetasql::Value::String("fulltext")});
  args.push_back(*fulltext_tokenlist);
  args.push_back(zetasql::Value::String("test"));

  EXPECT_THAT(
      ScoreNgramsEvaluator::Evaluate(args),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr(
              "SCORE_NGRAMS function's first argument must be a TOKENLIST "
              "column generated by TOKENIZE_SUBSTRING or TOKENIZE_NGRAMS")));
}

TEST(ScoreNgramsEvaluatorTest, EvaluateOnNullQueryReturnsZero) {
  std::vector<zetasql::Value> args;
  const auto ngrams_tokenlist =
      NgramsTokenizer::Tokenize({zetasql::Value::String("foo")});
  args.push_back(*ngrams_tokenlist);
  args.push_back(zetasql::Value::NullString());

  absl::StatusOr<zetasql::Value> result =
      ScoreNgramsEvaluator::Evaluate(args);
  ZETASQL_EXPECT_OK(result.status());

  zetasql::Value score = result.value();
  EXPECT_TRUE(score.type()->IsDouble());
  EXPECT_EQ(0.0, score.double_value());
}

TEST(ScoreNgramsEvaluatorTest, EvaluateOnNullTokenListReturnsZero) {
  std::vector<zetasql::Value> args;
  args.push_back(zetasql::Value::NullTokenList());
  args.push_back(zetasql::Value::String("test"));

  absl::StatusOr<zetasql::Value> result =
      ScoreNgramsEvaluator::Evaluate(args);
  ZETASQL_EXPECT_OK(result.status());

  zetasql::Value score = result.value();
  EXPECT_TRUE(score.type()->IsDouble());
  EXPECT_EQ(0.0, score.double_value());
}

struct ScoreNgramsEvaluatorTestCase {
  std::vector<std::string> tokens;
  std::string query;
  double expected_result;
};

using ScoreNgramsEvaluatorTest =
    ::testing::TestWithParam<ScoreNgramsEvaluatorTestCase>;

TEST_P(ScoreNgramsEvaluatorTest, TestScoreNgramsEvaluate) {
  const ScoreNgramsEvaluatorTestCase& test_case = GetParam();
  zetasql::Value query = zetasql::Value::String(test_case.query);
  std::vector<zetasql::Value> args{TokenListFromStrings(test_case.tokens),
                                     query};

  absl::StatusOr<zetasql::Value> result =
      ScoreNgramsEvaluator::Evaluate(args);
  ZETASQL_EXPECT_OK(result.status());

  zetasql::Value score = result.value();
  EXPECT_TRUE(score.type()->IsDouble());

  EXPECT_EQ(test_case.expected_result, score.double_value());
};

INSTANTIATE_TEST_SUITE_P(
    ScoreNgramsEvaluatorTestSuite, ScoreNgramsEvaluatorTest,
    testing::ValuesIn<ScoreNgramsEvaluatorTestCase>({
        {{"ngrams-3-3-0", "spa", "pan"}, "span", 1},
        {{"ngrams-3-3-0", "spa", "pan"}, "spaz", 0.33333333333333331},
        {{"substring-3-3-0", "foo", "bar"}, "foo+bar=baz", 0.22222222222222221},
        // Test cases for concatenated tokenlists
        {{"ngrams-3-3-0", "spa", "pan", kGapString, "ngrams-3-3-0", "clo",
          "lou", "oud"},
         "spancloud",
         0.7142857142857143},
        {{"ngrams-3-3-0", "spa", "pan", kGapString, "ngrams-3-3-0", "clo",
          "lou", "oud"},
         "spnaclodu",
         0.090909090909090912},
        {{"ngrams-3-3-0", "spa", "pan", kGapString, "substring-3-3-0", "foo",
          "bar"},
         "spaz;foo+bar=baz",
         0.2},
    }));

}  // namespace search
}  // namespace query
}  // namespace backend
}  // namespace emulator
}  // namespace spanner
}  // namespace google