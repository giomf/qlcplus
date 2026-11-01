/*
  Q Light Controller Plus - Unit test
  docsync_test.cpp

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

#include <QtTest>
#include <QSignalSpy>
#include <QXmlStreamWriter>

#include "docsync_test.h"
#include "chaserstep.h"
#include "scenevalue.h"
#include "docsync.h"
#include "chaser.h"
#include "scene.h"
#include "show.h"
#include "efx.h"
#include "doc.h"

void DocSync_Test::init()
{
    m_docA = new Doc(this);
    m_docB = new Doc(this);
    m_syncA = new DocSync(m_docA);
    m_syncB = new DocSync(m_docB);

    /* Each side receives what the other one sends, as over the network */
    connect(m_syncA, &DocSync::functionUpdated, m_syncB, &DocSync::applyFunction);
    connect(m_syncA, &DocSync::functionDeleted, m_syncB, &DocSync::applyFunctionRemoval);
    connect(m_syncB, &DocSync::functionUpdated, m_syncA, &DocSync::applyFunction);
    connect(m_syncB, &DocSync::functionDeleted, m_syncA, &DocSync::applyFunctionRemoval);
}

void DocSync_Test::cleanup()
{
    delete m_syncA;
    delete m_syncB;
    delete m_docA;
    delete m_docB;
}

void DocSync_Test::createScene()
{
    Scene *scene = new Scene(m_docA);
    scene->setName("Front wash");
    scene->setValue(1, 0, 255);
    QVERIFY(m_docA->addFunction(scene));
    QVERIFY(m_docB->function(scene->id()) == nullptr);

    m_syncA->flush();

    Scene *remote = qobject_cast<Scene*>(m_docB->function(scene->id()));
    QVERIFY(remote != nullptr);
    QCOMPARE(remote->name(), QString("Front wash"));
    QCOMPARE(remote->value(1, 0), uchar(255));
}

void DocSync_Test::changeSceneReplacesValues()
{
    Scene *scene = new Scene(m_docA);
    scene->setValue(1, 0, 255);
    scene->setValue(1, 1, 128);
    m_docA->addFunction(scene);
    m_syncA->flush();

    scene->unsetValue(1, 1);
    scene->setValue(1, 0, 10);
    m_syncA->flush();

    /* loadXML() alone would have kept the removed value */
    Scene *remote = qobject_cast<Scene*>(m_docB->function(scene->id()));
    QVERIFY(remote != nullptr);
    QCOMPARE(remote->value(1, 0), uchar(10));
    QVERIFY(remote->checkValue(SceneValue(1, 1)) == false);
}

void DocSync_Test::deleteFunction()
{
    Scene *scene = new Scene(m_docA);
    m_docA->addFunction(scene);
    quint32 id = scene->id();
    m_syncA->flush();
    QVERIFY(m_docB->function(id) != nullptr);

    m_docA->deleteFunction(id);
    m_syncA->flush();

    QVERIFY(m_docB->function(id) == nullptr);
}

void DocSync_Test::renameFunction()
{
    Scene *scene = new Scene(m_docA);
    scene->setName("Old");
    m_docA->addFunction(scene);
    m_syncA->flush();

    QSignalSpy nameSpy(m_docB, &Doc::functionNameChanged);
    scene->setName("New");
    m_syncA->flush();

    QCOMPARE(m_docB->function(scene->id())->name(), QString("New"));
    QCOMPARE(nameSpy.count(), 1);
}

void DocSync_Test::chaserSteps()
{
    Scene *s1 = new Scene(m_docA);
    Scene *s2 = new Scene(m_docA);
    m_docA->addFunction(s1);
    m_docA->addFunction(s2);
    Chaser *chaser = new Chaser(m_docA);
    m_docA->addFunction(chaser);
    chaser->addStep(ChaserStep(s1->id()));
    chaser->addStep(ChaserStep(s2->id()));
    m_syncA->flush();

    Chaser *remote = qobject_cast<Chaser*>(m_docB->function(chaser->id()));
    QVERIFY(remote != nullptr);
    QCOMPARE(remote->stepsCount(), 2);
    QVERIFY(m_docB->function(s1->id()) != nullptr);
    QVERIFY(m_docB->function(s2->id()) != nullptr);

    chaser->removeStep(0);
    m_syncA->flush();

    QCOMPARE(remote->stepsCount(), 1);
    QCOMPARE(remote->steps().at(0).fid, s2->id());
}

void DocSync_Test::efxParameters()
{
    EFX *efx = new EFX(m_docA);
    m_docA->addFunction(efx);
    m_syncA->flush();

    efx->setWidth(42);
    efx->setAlgorithm(EFX::Eight);
    m_syncA->flush();

    EFX *remote = qobject_cast<EFX*>(m_docB->function(efx->id()));
    QVERIFY(remote != nullptr);
    QCOMPARE(remote->width(), efx->width());
    QCOMPARE(remote->algorithm(), EFX::Eight);
}

void DocSync_Test::noEchoOfRemoteChanges()
{
    QSignalSpy updatedB(m_syncB, &DocSync::functionUpdated);
    QSignalSpy deletedB(m_syncB, &DocSync::functionDeleted);

    Scene *scene = new Scene(m_docA);
    m_docA->addFunction(scene);
    quint32 id = scene->id();
    m_syncA->flush();
    scene->setValue(1, 0, 50);
    m_syncA->flush();
    m_docA->deleteFunction(id);
    m_syncA->flush();

    m_syncB->flush();
    QCOMPARE(updatedB.count(), 0);
    QCOMPARE(deletedB.count(), 0);
}

void DocSync_Test::unchangedSnapshotNotSent()
{
    Scene *scene = new Scene(m_docA);
    m_docA->addFunction(scene);
    m_syncA->flush();

    QSignalSpy updatedA(m_syncA, &DocSync::functionUpdated);
    emit scene->changed(scene->id());
    m_syncA->flush();

    QCOMPARE(updatedA.count(), 0);
}

void DocSync_Test::createdAndDeletedBeforeFlush()
{
    QSignalSpy updatedA(m_syncA, &DocSync::functionUpdated);
    QSignalSpy deletedA(m_syncA, &DocSync::functionDeleted);

    Scene *scene = new Scene(m_docA);
    m_docA->addFunction(scene);
    m_docA->deleteFunction(scene->id());
    m_syncA->flush();

    QCOMPARE(updatedA.count(), 0);
    QCOMPARE(deletedA.count(), 0);
}

void DocSync_Test::disabledSendsNothing()
{
    QSignalSpy updatedA(m_syncA, &DocSync::functionUpdated);

    m_syncA->setEnabled(false);
    m_docA->addFunction(new Scene(m_docA));
    m_syncA->flush();

    QCOMPARE(updatedA.count(), 0);
}

void DocSync_Test::typeMismatchIgnored()
{
    /* Both sides created a different Function with the same ID */
    m_syncB->setEnabled(false);
    QVERIFY(m_docB->addFunction(new Scene(m_docB), 7));
    QVERIFY(m_docA->addFunction(new Chaser(m_docA), 7));

    m_syncA->flush();

    QCOMPARE(m_docB->function(7)->type(), Function::SceneType);
}

void DocSync_Test::deleteAndRecreateSameId()
{
    Scene *scene = new Scene(m_docA);
    m_docA->addFunction(scene);
    quint32 id = scene->id();
    m_syncA->flush();

    /* What undoing a delete does, within a single batch */
    m_docA->deleteFunction(id);
    QVERIFY(m_docA->addFunction(new Scene(m_docA), id));
    m_syncA->flush();

    QVERIFY(m_docB->function(id) != nullptr);
}

void DocSync_Test::declaredTypeMismatchRejected()
{
    Scene *scene = new Scene(m_docA);
    m_docA->addFunction(scene);

    QByteArray xml;
    QXmlStreamWriter writer(&xml);
    scene->saveXML(&writer);

    /* e.g. a Show declared as something else to get past an access check */
    m_syncB->applyFunction(scene->id(), Function::ChaserType, xml);

    QVERIFY(m_docB->function(scene->id()) == nullptr);
}

void DocSync_Test::showChangesNotSynced()
{
    Show *show = new Show(m_docA);
    show->setName("Concert");
    m_docA->addFunction(show);
    m_syncA->flush();

    /* Creation is synced */
    QVERIFY(m_docB->function(show->id()) != nullptr);
    int functionsB = m_docB->functions().size();

    /* A change isn't: Show::copyFrom() would duplicate tracks and items */
    QSignalSpy updatedA(m_syncA, &DocSync::functionUpdated);
    show->setName("Festival");
    m_syncA->flush();
    QCOMPARE(updatedA.count(), 0);

    /* and a snapshot for an existing Show is ignored */
    QByteArray xml;
    QXmlStreamWriter writer(&xml);
    show->saveXML(&writer);
    m_syncB->applyFunction(show->id(), Function::ShowType, xml);

    QCOMPARE(m_docB->function(show->id())->name(), QString("Concert"));
    QCOMPARE(m_docB->functions().size(), functionsB);
}

QTEST_GUILESS_MAIN(DocSync_Test)
