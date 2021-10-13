## UnityIssue1154TestApp

By: Denver Coneybeare <dconeybe@google.com>

Oct 13, 2021

### Background Information

This is a C++ application to attempt to reproduce
https://github.com/firebase/quickstart-unity/issues/1154
in the Firebase C++ SDK to facilitate debugging.

The issue reported by @dex3r is that if a read is the first Firestore operation
performed then the read fails with the following error:

> FirestoreException: Failed to get document from server.

However, if the first operation is, instead, a _write_ then the subsequent read
succeeds.

Based on the logs provided by @dex3r, it appears that the initial connection
with the Firestore backend is taking upwards of 20 seconds, which exceeds the
10-second timeout for reads. Writes, on the other hand, have a less aggressive
timeout and can tolerate this excessively-long initial connection time.

This test app attempts to reproduce the issue at a lower level to more easily
faciliate debugging.

### License

Copyright 2021 Google LLC

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
