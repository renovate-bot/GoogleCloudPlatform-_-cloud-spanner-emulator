#
# Copyright 2020 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

load(
    "//backend/common:javacc_parser.bzl",
    "generate_javacc_parser",
)

package(
    default_visibility = ["//:__subpackages__"],
)

licenses(["notice"])

cc_library(
    name = "ddl_parser",
    srcs = [
        "ddl_includes.h",
        "ddl_parser.cc",
    ],
    hdrs = [
        "ddl_parser.h",
    ],
    deps = [
        ":javacc_ddl_parser",
        "//backend/common:utils",
        "//backend/schema/ddl:operations_cc_proto",
        "//common:constants",
        "//common:errors",
        "//common:feature_flags",
        "//common:limits",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_zetasql//zetasql/base:no_destructor",
    ],
)

cc_test(
    name = "ddl_parser_test",
    srcs = ["ddl_parser_test.cc"],
    deps = [
        ":ddl_parser",
        "//backend/schema/catalog:proto_bundle",
        "//common:feature_flags",
        "//tests/common:proto_matchers",
        "//tests/common:scoped_feature_flags_setter",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
        "@com_google_zetasql//zetasql/base/testing:status_matchers",
    ],
)

cc_library(
    name = "ddl_reserved_words",
    srcs = ["ddl_reserved_words.cc"],
    hdrs = [
        "ddl_reserved_words.h",
    ],
    deps = [
        "//backend/common:case",
        "@com_google_absl//absl/strings",
    ],
)

cc_binary(
    name = "ddl_keywords",
    srcs = [
        "ddl_keywords.cc",
    ],
    deps = [
        ":ddl_reserved_words",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/strings",
        "@com_google_zetasql//zetasql/base",
    ],
)

genrule(
    name = "ddl_keywords_jjt",
    outs = ["ddl_keywords.jjt"],
    cmd = "$(location :ddl_keywords) > $@",
    tools = [":ddl_keywords"],
)

genrule(
    name = "copy_node_base_header",
    srcs = ["ddl_node_base.h"],
    outs = ["NodeBase.h"],
    cmd = "cat $(SRCS) > $(OUTS)",
)

generate_javacc_parser(
    name = "javacc_ddl_parser",
    # Do not sort: "ddl_parser_cpp.inc" must come before ".jjt".
    srcs = [
        "ddl_parser_cpp.inc",
        "ddl_whitespace.jjt",
        "ddl_keywords.jjt",
        "ddl_string_bytes_tokens.jjt",
        "ddl_expression.jjt",
        "ddl_parser.jjt",
    ],
    extra_deps = [
        "@com_google_zetasql//zetasql/public:strings",
    ],
    extra_headers = [
        "ddl_char_stream.h",
        "ddl_node_base.h",
        "ddl_token_base.h",
        "ddl_token_validation.h",
        "ddl_token_validation_utils.h",
    ],
    extra_srcs = [
        "ddl_token_validation.cc",
        "ddl_token_validation_utils.cc",
        "NodeBase.h",
    ],
    parser_class_name = "DDLParser",
)
