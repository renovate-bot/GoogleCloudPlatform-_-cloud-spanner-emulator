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

package(
    default_visibility = ["//:__subpackages__"],
)

licenses(["notice"])

cc_library(
    name = "clock",
    srcs = ["clock.cc"],
    hdrs = ["clock.h"],
    deps = [
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
    ],
)

cc_test(
    name = "clock_test",
    srcs = ["clock_test.cc"],
    deps = [
        ":clock",
        "//tests/common:proto_matchers",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googletest//:gtest_main",
        "@com_google_zetasql//zetasql/base/testing:status_matchers",
    ],
)

cc_library(
    name = "constants",
    hdrs = [
        "constants.h",
    ],
    deps = ["@com_google_zetasql//zetasql/public:type"],
)

cc_library(
    name = "change_stream",
    srcs = [
        "change_stream.cc",
    ],
    hdrs = [
        "change_stream.h",
    ],
    deps = [
        ":constants",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "errors",
    srcs = [
        "errors.cc",
    ],
    hdrs = [
        "errors.h",
    ],
    deps = [
        ":constants",
        ":limits",
        "//backend/common:ids",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
        "@com_google_absl//absl/time",
        "@com_google_googleapis//google/rpc:error_details_cc_proto",
    ],
)

cc_library(
    name = "limits",
    hdrs = [
        "limits.h",
    ],
)

cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = ["@com_google_absl//absl/flags:flag"],
)

cc_library(
    name = "feature_flags",
    hdrs = ["feature_flags.h"],
    deps = [
        "@com_google_absl//absl/synchronization",
    ],
)

cc_test(
    name = "feature_flags_test",
    srcs = ["feature_flags_test.cc"],
    deps = [
        ":feature_flags",
        "//tests/common:proto_matchers",
        "//tests/common:scoped_feature_flags_setter",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googletest//:gtest_main",
        "@com_google_zetasql//zetasql/base/testing:status_matchers",
    ],
)

cc_library(
    name = "bit_reverse",
    srcs = [
        "bit_reverse.cc",
    ],
    hdrs = [
        "bit_reverse.h",
    ],
    deps = [
        "@com_google_zetasql//zetasql/base:bits",
    ],
)
