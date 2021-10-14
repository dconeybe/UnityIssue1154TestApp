# Copyright 2021 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.20)

project(firebase_unity_issue_1154_test_app)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build a desktop application.
add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)

# Windows runtime mode, either MD or MT depending on whether you are using
# /MD or /MT. For more information see:
# https://msdn.microsoft.com/en-us/library/2kzt1wy3.aspx
set(MSVC_RUNTIME_MODE MD)

if(APPLE)
  set(PLATFORM_LIBS
    gssapi_krb5
    pthread
    "-framework CoreFoundation"
    "-framework Foundation"
    "-framework GSS"
    "-framework Security"
    "-framework SystemConfiguration"
  )
elseif(MSVC)
  set(PLATFORM_LIBS advapi32 ws2_32 crypt32)
else()
  set(PLATFORM_LIBS pthread)
endif()

add_executable(
  firebase_unity_issue_1154_test_app
  main.cc
)

# Add Firebase libraries to the target using the function from the SDK.
add_subdirectory(firebase_cpp_sdk bin/ EXCLUDE_FROM_ALL)

# The Firebase C++ library `firebase_app` is required,
# and it must always be listed last.
target_link_libraries(
  firebase_unity_issue_1154_test_app
  firebase_auth
  firebase_firestore
  firebase_app
  ${PLATFORM_LIBS}
)
