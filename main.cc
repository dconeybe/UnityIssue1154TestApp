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

#include <condition_variable>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "firebase/app.h"
#include "firebase/firestore.h"

using ::firebase::App;
using ::firebase::AppOptions;
using ::firebase::Future;
using ::firebase::FutureBase;
using ::firebase::FutureStatus;
using ::firebase::firestore::DocumentReference;
using ::firebase::firestore::DocumentSnapshot;
using ::firebase::firestore::Error;
using ::firebase::firestore::FieldValue;
using ::firebase::firestore::Firestore;
using ::firebase::firestore::MapFieldValue;
using ::firebase::firestore::Source;

enum class Operation {
  kRead,
  kWrite,
};

template <typename T>
void Log(T message) {
  std::cout << message << std::endl;
}

template <typename T, typename... U>
void Log(T message, U... rest) {
  std::cout << message;
  Log(rest...);
}

class AwaitableFutureCompletion {
 public:
  AwaitableFutureCompletion(FutureBase& future) : future_(future) {
    future.OnCompletion(OnCompletion);
  }

  void AwaitInvoked() const {
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait(lock, [this](){return future_.status() != FutureStatus::kFutureStatusPending; });
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
  ArgParseException(const std::string& what) : what_(what) {
  }

  const char* what() const noexcept override{
    return what_.c_str();
  }

 private:
  const std::string what_;
};

std::vector<Operation> ParseOperations(int argc, char** argv) {
  std::vector<Operation> operations;
  for (int i=1; i<argc; i++) {
    std::string operation_name(argv[i]);
    if (operation_name == "read") {
      operations.push_back(Operation::kRead);
    } else if (operation_name == "write") {
      operations.push_back(Operation::kWrite);
    } else {
      throw ArgParseException(std::string("invalid argument: ")
        + operation_name
        + " (must be either \"read\" or \"write\")"
      );
    }
  }

  if (operations.size() == 0) {
    throw ArgParseException("No arguments specified; "
      "one or more of \"read\" or \"write\" is required");
  }

  return operations;
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
  AwaitableFutureCompletion completion(future);
  completion.AwaitInvoked();

  if (future.error() != Error::kErrorOk) {
    Log(name, " FAILED: ", FirestoreErrorNameFromErrorCode(future.error()), " ", future.error_message());
  } else {
    Log(name, " done");
  }
}

void DoRead(DocumentReference doc) {
  Log("*** DoRead() doc=", doc.path());
  Future<DocumentSnapshot> future = doc.Get(Source::kServer);
  AwaitCompletion(future, "DocumentReference.Get()");

  const DocumentSnapshot* snapshot = future.result();
  MapFieldValue data = snapshot->GetData(DocumentSnapshot::ServerTimestampBehavior::kDefault);
  Log("Document # key/value pairs: ", data.size());
  int entry_index = 0;
  for (const std::pair<std::string, FieldValue>& entry : data) {
    Log("Entry #", ++entry_index, ": ", entry.first, "=", entry.second);
  }
}

void DoWrite(DocumentReference doc) {
  Log("*** DoWrite() doc=", doc.path());
  MapFieldValue map;
  map["meaning"] = FieldValue::Integer(42);
  Future<void> future = doc.Set(map);
  AwaitCompletion(future, "DocumentReference.Set()");
}

int main(int argc, char** argv) {
  std::vector<Operation> operations;
  try {
    operations = ParseOperations(argc, argv);
  } catch (ArgParseException& e) {
    Log("ERROR: Invalid command-line arguments: ", e.what());
    return 2;
  }

  Log("Creating firebase::App");
  std::unique_ptr<App> app(App::Create(AppOptions()));
  if (! app) {
    Log("ERROR: Creating firebase::App FAILED!");
    return 1;
  }

  Log("Creating firebase::firestore::Firestore");
  std::unique_ptr<Firestore> firestore(Firestore::GetInstance(app.get(), nullptr));
  if (! firestore) {
    Log("ERROR: Creating firebase::firestore::Firestore FAILED!");
    return 1;
  }

  DocumentReference doc = firestore->Document("UnityIssue1154TestApp/TestDoc");
  Log("Performing ", operations.size(), " operations on document: ", doc.path());
  for (Operation operation : operations) {
    switch (operation) {
      case Operation::kRead:
        DoRead(doc);
        break;
      case Operation::kWrite:
        DoWrite(doc);
        break;
      default:
        Log("INTERNAL ERROR: unknown value for operation: ", static_cast<int>(operation));
        return 1;
    }
  }

  return 0;
}
