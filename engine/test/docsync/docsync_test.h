/*
  Q Light Controller Plus - Unit test
  docsync_test.h

  Copyright (c) Guillaume Fournier

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#ifndef DOCSYNC_TEST_H
#define DOCSYNC_TEST_H

#include <QObject>

class DocSync;
class Doc;

/* Two Docs, each with a DocSync, wired to each other as a network would */
class DocSync_Test final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void createScene();
    void changeSceneReplacesValues();
    void deleteFunction();
    void renameFunction();
    void chaserSteps();
    void efxParameters();
    void noEchoOfRemoteChanges();
    void unchangedSnapshotNotSent();
    void createdAndDeletedBeforeFlush();
    void disabledSendsNothing();
    void typeMismatchIgnored();
    void deleteAndRecreateSameId();
    void declaredTypeMismatchRejected();
    void showChangesNotSynced();

private:
    Doc *m_docA;
    Doc *m_docB;
    DocSync *m_syncA;
    DocSync *m_syncB;
};

#endif
