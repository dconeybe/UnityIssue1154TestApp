/*
 * Copyright 2021 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cctype>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "firebase/app.h"
#include "firebase/firestore.h"

using ::firebase::App;
using ::firebase::AppOptions;
using ::firebase::Future;
using ::firebase::FutureBase;
using ::firebase::FutureStatus;
using ::firebase::LogLevel;
using ::firebase::SetLogLevel;
using ::firebase::firestore::DocumentReference;
using ::firebase::firestore::DocumentSnapshot;
using ::firebase::firestore::Error;
using ::firebase::firestore::FieldValue;
using ::firebase::firestore::Firestore;
using ::firebase::firestore::MapFieldValue;
using ::firebase::firestore::Settings;
using ::firebase::firestore::Source;

enum class Operation {
  kRead,
  kWrite,
};

std::string FormattedTimestamp() {
  auto timestamp = std::chrono::system_clock::now();
  std::time_t ctime_timestamp = std::chrono::system_clock::to_time_t(timestamp);
  std::string formatted_timestamp(std::ctime(&ctime_timestamp));
  while (formatted_timestamp.size() > 0) {
    auto last_char = formatted_timestamp[formatted_timestamp.size() - 1];
    if (!std::isspace(last_char)) {
      break;
    }
    formatted_timestamp.pop_back();
  }
  return formatted_timestamp;
}

template <typename T>
void Log0(T message) {
  std::cout << message << std::endl;
}

template <typename T, typename... U>
void Log0(T message, U... rest) {
  std::cout << message;
  Log0(rest...);
}

template <typename... T>
void Log(T... messages) {
  std::cout << ">>>>> " << FormattedTimestamp() << " -- ";
  Log0(messages...);
}

class AwaitableFutureCompletion {
 public:
  AwaitableFutureCompletion(FutureBase& future) : future_(future) {
    future.OnCompletion(OnCompletion);
  }

  void AwaitInvoked() const {
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait(lock, [this]() {
      return future_.status() != FutureStatus::kFutureStatusPending;
    });
  }

 private:
  static void OnCompletion(const FutureBase&) {
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.notify_all();
  }

  static std::mutex mutex_;
  static std::condition_variable condition_;
  FutureBase& future_;
};

std::mutex AwaitableFutureCompletion::mutex_;
std::condition_variable AwaitableFutureCompletion::condition_;

class ArgParseException : public std::exception {
 public:
  ArgParseException(const std::string& what) : what_(what) {}

  const char* what() const noexcept override { return what_.c_str(); }

 private:
  const std::string what_;
};

struct ParsedArguments {
  std::vector<Operation> operations;
  std::string key;
  bool key_valid = false;
  std::string value;
  bool value_valid = false;
  bool use_emulator = false;
  bool debug_logging_enabled = false;
  std::string help_text;
};

ParsedArguments ParseArguments(int argc, char** argv) {
  ParsedArguments args;
  bool next_is_key = false;
  bool next_is_value = false;
  bool show_help = false;

  for (int i = 1; i < argc; i++) {
    std::string arg(argv[i]);
    if (next_is_key) {
      args.key = arg;
      args.key_valid = true;
      next_is_key = false;
    } else if (next_is_value) {
      args.value = arg;
      args.value_valid = true;
      next_is_value = false;
    } else if (arg == "read") {
      args.operations.push_back(Operation::kRead);
    } else if (arg == "write") {
      args.operations.push_back(Operation::kWrite);
    } else if (arg == "-k" || arg == "--key") {
      next_is_key = true;
    } else if (arg == "-v" || arg == "--value") {
      next_is_value = true;
    } else if (arg == "-e" || arg == "--emulator") {
      args.use_emulator = true;
    } else if (arg == "-d" || arg == "--debug") {
      args.debug_logging_enabled = true;
    } else if (arg == "-h" || arg == "--help") {
      show_help = true;
    } else {
      throw ArgParseException(std::string("invalid argument: ") + arg +
                              " (run with --help for help)");
    }
  }

  if (next_is_key) {
    throw ArgParseException("expected argument after --key");
  } else if (next_is_value) {
    throw ArgParseException("expected argument after --value");
  } else if (args.operations.size() == 0 && !show_help) {
    throw ArgParseException("no arguments specified; run with --help for help");
  }

  if (show_help) {
    std::ostringstream ss;
    ss << "Syntax: " << argv[0] << " [options] <read|write>..." << std::endl;
    ss << std::endl;
    ss << "The arguments \"read\" and \"write\" may be specified" << std::endl;
    ss << "one or more times each, and each occurrence causes" << std::endl;
    ss << "the application to perform a read or write operation" << std::endl;
    ss << "from Firestore, respectively." << std::endl;
    ss << std::endl;
    ss << "The current directory *must* contain a" << std::endl;
    ss << "google-services.json file." << std::endl;
    ss << std::endl;
    ss << "Options:" << std::endl;
    ss << "  -h/--help" << std::endl;
    ss << "    Print this help message and exit." << std::endl;
    ss << "  -k/--key" << std::endl;
    ss << "    Use this key when writing to Firestore." << std::endl;
    ss << "  -v/--value" << std::endl;
    ss << "    Use this value when writing to Firestore." << std::endl;
    ss << "  -e/--emulator" << std::endl;
    ss << "    Connection to the Firestore emulator." << std::endl;
    ss << "  -d/--debug" << std::endl;
    ss << "    Enable Firebase/Firestore debug logging." << std::endl;
    ss << std::endl;
    ss << "Examples:" << std::endl;
    ss << std::endl;
    ss << "Example 1: Perform a read followed by a write:" << std::endl;
    ss << argv[0] << " read write" << std::endl;
    ss << std::endl;
    ss << "Example 2: Perform a write with custom key/value pair:" << std::endl;
    ss << argv[0] << " -k city -v Dallas write" << std::endl;
    ss << std::endl;
    ss << "Example 3: Enable debug logging:" << std::endl;
    ss << argv[0] << " --debug read write" << std::endl;
    args.help_text = ss.str();
  }

  return args;
}

std::string FirestoreErrorNameFromErrorCode(int error) {
  switch (error) {
    case Error::kErrorOk:
      return "kErrorOk";
    case Error::kErrorCancelled:
      return "kErrorCancelled";
    case Error::kErrorUnknown:
      return "kErrorUnknown";
    case Error::kErrorInvalidArgument:
      return "kErrorInvalidArgument";
    case Error::kErrorDeadlineExceeded:
      return "kErrorDeadlineExceeded";
    case Error::kErrorNotFound:
      return "kErrorNotFound";
    case Error::kErrorAlreadyExists:
      return "kErrorAlreadyExists";
    case Error::kErrorPermissionDenied:
      return "kErrorPermissionDenied";
    case Error::kErrorResourceExhausted:
      return "kErrorResourceExhausted";
    case Error::kErrorFailedPrecondition:
      return "kErrorFailedPrecondition";
    case Error::kErrorAborted:
      return "kErrorAborted";
    case Error::kErrorOutOfRange:
      return "kErrorOutOfRange";
    case Error::kErrorUnimplemented:
      return "kErrorUnimplemented";
    case Error::kErrorInternal:
      return "kErrorInternal";
    case Error::kErrorUnavailable:
      return "kErrorUnavailable";
    case Error::kErrorDataLoss:
      return "kErrorDataLoss";
    case Error::kErrorUnauthenticated:
      return "kErrorUnauthenticated";
    default:
      return std::to_string(static_cast<int>(error));
  }
}

void AwaitCompletion(FutureBase& future, const std::string& name) {
  Log(name, " start");
  auto start = std::chrono::steady_clock::now();
  AwaitableFutureCompletion completion(future);
  completion.AwaitInvoked();
  auto end = std::chrono::steady_clock::now();
  std::chrono::duration<double> elapsed_seconds = end - start;

  std::string elapsed_time_str;
  {
    std::ostringstream ss;
    ss << std::fixed;
    ss << std::setprecision(2);
    ss << elapsed_seconds.count() << "s";
    elapsed_time_str = ss.str();
  }

  if (future.error() != Error::kErrorOk) {
    Log(name, " FAILED in ", elapsed_time_str, ": ",
        FirestoreErrorNameFromErrorCode(future.error()), " ",
        future.error_message());
  } else {
    Log(name, " done in ", elapsed_time_str);
  }
}

void DoRead(DocumentReference doc) {
  Log("=======================================");
  Log("DoRead() doc=", doc.path());
  Future<DocumentSnapshot> future = doc.Get(Source::kServer);
  AwaitCompletion(future, "DocumentReference.Get()");

  const DocumentSnapshot* snapshot = future.result();
  MapFieldValue data =
      snapshot->GetData(DocumentSnapshot::ServerTimestampBehavior::kDefault);
  Log("Document num key/value pairs: ", data.size());
  int entry_index = 0;
  for (const std::pair<std::string, FieldValue>& entry : data) {
    Log("Entry #", ++entry_index, ": ", entry.first, "=", entry.second);
  }
}

void DoWrite(DocumentReference doc, const std::string& key,
             const std::string& value) {
  Log("=======================================");
  Log("DoWrite() doc=", doc.path(), " setting ", key, "=", value);
  MapFieldValue map;
  map[key] = FieldValue::String(value);
  Future<void> future = doc.Set(map);
  AwaitCompletion(future, "DocumentReference.Set()");
}

int main(int argc, char** argv) {
  ParsedArguments args;
  try {
    args = ParseArguments(argc, argv);
  } catch (ArgParseException& e) {
    Log("ERROR: Invalid command-line arguments: ", e.what());
    return 2;
  }

  if (args.help_text.size() > 0) {
    std::cout << args.help_text;
    return 0;
  }

  if (args.debug_logging_enabled) {
    Log("Enabling debug logging");
    SetLogLevel(LogLevel::kLogLevelDebug);
    Firestore::set_log_level(LogLevel::kLogLevelDebug);
  }

  Log("Creating firebase::App");
  std::unique_ptr<App> app(App::Create(AppOptions()));
  if (!app) {
    Log("ERROR: Creating firebase::App FAILED!");
    return 1;
  }

  Log("Creating firebase::firestore::Firestore");
  std::unique_ptr<Firestore> firestore(
      Firestore::GetInstance(app.get(), nullptr));
  if (!firestore) {
    Log("ERROR: Creating firebase::firestore::Firestore FAILED!");
    return 1;
  }

  if (args.use_emulator) {
    Log("Using the Firestore Emulator");
    Settings settings = firestore->settings();
    settings.set_host("localhost:8080");
    settings.set_ssl_enabled(false);
    firestore->set_settings(settings);
  }

  DocumentReference doc = firestore->Document("UnityIssue1154TestApp/TestDoc");
  Log("Performing ", args.operations.size(),
      " operations on document: ", doc.path());
  for (Operation operation : args.operations) {
    switch (operation) {
      case Operation::kRead: {
        DoRead(doc);
        break;
      }
      case Operation::kWrite: {
        std::string key = args.key_valid ? args.key : "TestKey";
        std::string value = args.value_valid ? args.value : "TestValue";
        DoWrite(doc, key, value);
        break;
      }
      default: {
        Log("INTERNAL ERROR: unknown value for operation: ",
            static_cast<int>(operation));
        return 1;
      }
    }
  }

  return 0;
}
