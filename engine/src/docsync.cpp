/*
  Q Light Controller Plus
  docsync.cpp

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

#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QSignalBlocker>
#include <QDebug>

#include "inputoutputmap.h"
#include "function.h"
#include "docsync.h"
#include "doc.h"

/* Changes closer than this are sent as a single snapshot */
#define FLUSH_DELAY_MS  100

DocSync::DocSync(Doc *doc, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_enabled(true)
    , m_applyingRemote(false)
    , m_clearing(false)
{
    Q_ASSERT(m_doc != nullptr);

    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(FLUSH_DELAY_MS);
    connect(&m_flushTimer, &QTimer::timeout, this, &DocSync::flush);

    connect(m_doc, &Doc::functionAdded, this, &DocSync::slotFunctionAdded);
    connect(m_doc, &Doc::functionChanged, this, &DocSync::slotFunctionChanged);
    connect(m_doc, &Doc::functionNameChanged, this, &DocSync::slotFunctionChanged);
    connect(m_doc, &Doc::functionRemoved, this, &DocSync::slotFunctionRemoved);
    connect(m_doc, &Doc::clearing, this, &DocSync::slotClearing);
    connect(m_doc, &Doc::cleared, this, &DocSync::slotCleared);
}

void DocSync::setEnabled(bool enable)
{
    if (enable == m_enabled)
        return;

    m_enabled = enable;

    /* Snapshots taken while disabled may be stale for the peer, which
     * gets the whole project again when a connection is made */
    if (enable == false)
        clearPending();
}

bool DocSync::isEnabled() const
{
    return m_enabled;
}

bool DocSync::isListening() const
{
    return m_enabled && m_applyingRemote == false && m_clearing == false &&
           m_doc->loadStatus() != Doc::Loading;
}

void DocSync::scheduleFlush()
{
    /* Not restarted on every change: during a continuous edit (e.g. a fader
     * drag) the peer must still get a snapshot every FLUSH_DELAY_MS */
    if (m_flushTimer.isActive() == false)
        m_flushTimer.start();
}

void DocSync::clearPending()
{
    m_flushTimer.stop();
    m_added.clear();
    m_changed.clear();
    m_removed.clear();
    m_lastXml.clear();
}

/*****************************************************************************
 * Local changes
 *****************************************************************************/

void DocSync::slotFunctionAdded(quint32 id)
{
    if (isListening() == false)
        return;

    m_added.append(id);
    scheduleFlush();
}

void DocSync::slotFunctionChanged(quint32 id)
{
    if (isListening() == false)
        return;

    m_changed.insert(id);
    scheduleFlush();
}

void DocSync::slotFunctionRemoved(quint32 id)
{
    if (isListening() == false)
        return;

    m_changed.remove(id);
    m_lastXml.remove(id);

    /* Created and deleted before the peer ever heard of it */
    if (m_added.removeAll(id) > 0)
        return;

    m_removed.append(id);
    scheduleFlush();
}

void DocSync::slotClearing()
{
    clearPending();
    m_clearing = true;
}

void DocSync::slotCleared()
{
    m_clearing = false;
}

QByteArray DocSync::functionXML(quint32 id) const
{
    Function *function = m_doc->function(id);
    if (function == nullptr)
        return QByteArray();

    QByteArray xml;
    QXmlStreamWriter writer(&xml);
    function->saveXML(&writer);

    return xml;
}

void DocSync::flush()
{
    m_flushTimer.stop();

    /* Deletions first: a Function deleted and recreated with the same ID
     * (e.g. undoing a delete) must end up existing on the peer */
    for (quint32 id : std::as_const(m_removed))
        emit functionDeleted(id);

    /* Then creations in creation order, so that a peer never gets a
     * reference to a Function it doesn't know yet (e.g. a Sequence's
     * bound Scene) */
    QList<quint32> ids = m_added;
    for (quint32 id : std::as_const(m_changed))
    {
        if (m_added.contains(id))
            continue;

        /* See the class description */
        Function *function = m_doc->function(id);
        if (function != nullptr && function->type() == Function::ShowType)
            continue;

        ids.append(id);
    }

    for (quint32 id : std::as_const(ids))
    {
        QByteArray xml = functionXML(id);
        if (xml.isEmpty() || m_lastXml.value(id) == xml)
            continue;

        m_lastXml[id] = xml;
        emit functionUpdated(id, m_doc->function(id)->type(), xml);
    }

    m_added.clear();
    m_changed.clear();
    m_removed.clear();
}

/*****************************************************************************
 * Remote changes
 *****************************************************************************/

void DocSync::beginRemoteChange()
{
    m_applyingRemote = true;
}

void DocSync::endRemoteChange()
{
    m_applyingRemote = false;
}

void DocSync::applyFunction(quint32 id, int type, const QByteArray &xml)
{
    Function *function = m_doc->function(id);

    /* See the class description */
    if (function != nullptr && function->type() == Function::ShowType)
        return;

    QXmlStreamReader reader(xml);
    if (reader.readNextStartElement() == false)
    {
        qWarning() << Q_FUNC_INFO << "Invalid snapshot for Function" << id;
        return;
    }

    /* loadXML() adds to what a Function already holds, so load the
     * snapshot into a new instance and copy it over the existing one */
    Function *snapshot = Function::createFromXML(reader, m_doc);
    if (snapshot == nullptr)
        return;

    /* The declared type may have been used to grant access */
    if (snapshot->type() != type)
    {
        qWarning() << Q_FUNC_INFO << "Snapshot of Function" << id << "is a"
                   << Function::typeToString(snapshot->type()) << "but was declared as a"
                   << Function::typeToString(Function::Type(type)) << ". Rejected";
        delete snapshot;
        return;
    }

    beginRemoteChange();

    if (function == nullptr)
    {
        if (m_doc->addFunction(snapshot, id))
            snapshot = nullptr;
        else
            qWarning() << Q_FUNC_INFO << "Cannot create Function" << id;
    }
    else if (function->type() != snapshot->type())
    {
        /* Two peers created different Functions with the same ID at the same
         * time. Fix with server-assigned IDs if it happens in practice */
        qWarning() << Q_FUNC_INFO << "Function" << id << "is a"
                   << Function::typeToString(function->type()) << "here but a"
                   << Function::typeToString(snapshot->type()) << "on the peer. Ignored";
    }
    else
    {
        bool renamed = function->name() != snapshot->name();

        /* copyFrom() takes no locks and the Function may be running: hold the
         * universes, which MasterTimer keeps locked for a whole tick, so no
         * write() happens meanwhile. Signals are held back until the lock is
         * released, since their receivers may claim the universes as well */
        {
            QSignalBlocker blocker(function);
            m_doc->inputOutputMap()->claimUniverses();
            function->copyFrom(snapshot);
            m_doc->inputOutputMap()->releaseUniverses(false);
        }

        emit function->changed(id);
        if (renamed)
            emit function->nameChanged(id);
    }

    endRemoteChange();

    m_lastXml[id] = xml;
    delete snapshot;
}

void DocSync::applyFunctionRemoval(quint32 id)
{
    Function *function = m_doc->function(id);
    if (function == nullptr)
        return;

    beginRemoteChange();

    if (function->isRunning())
        function->stopAndWait();
    m_doc->deleteFunction(id);

    endRemoteChange();

    m_lastXml.remove(id);
}
