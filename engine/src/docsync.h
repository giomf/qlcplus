/*
  Q Light Controller Plus
  docsync.h

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

#ifndef DOCSYNC_H
#define DOCSYNC_H

#include <QByteArray>
#include <QObject>
#include <QTimer>
#include <QHash>
#include <QList>
#include <QSet>

class Doc;

/** @addtogroup engine Engine
 * @{
 */

/**
 * DocSync turns Doc's Function signals into whole-Function XML snapshots that
 * can be sent to another instance, and applies snapshots received from it.
 *
 * Any change that reaches Doc through a Function's changed() signal is
 * picked up, regardless of which part of the UI made it. Changes are batched
 * for a short time, so a fader drag results in a single snapshot.
 *
 * DocSync knows nothing about the network: the owner connects its signals
 * to a transport and feeds received data to its slots.
 *
 * Shows are only synced when created or deleted: Show::copyFrom() duplicates
 * tracks and items instead of replacing them, so changes to an existing Show
 * must be synced some other way.
 */
class DocSync final : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(DocSync)

public:
    DocSync(Doc *doc, QObject *parent = nullptr);

    /** While disabled, local changes are neither collected nor emitted */
    void setEnabled(bool enable);
    bool isEnabled() const;

    /** Emit the pending changes now instead of waiting for the batch timer */
    void flush();

    /**
     * Changes made between these calls come from another instance and are
     * not sent back. applyFunction() and applyFunctionRemoval() use them
     * internally; a front end needs them only when it applies a remote change
     * through its own code, e.g. a delete that also closes an open editor.
     */
    void beginRemoteChange();
    void endRemoteChange();

signals:
    /** A Function was created or changed. @a xml is its saveXML() output */
    void functionUpdated(quint32 id, int type, const QByteArray &xml);

    /** A Function was deleted */
    void functionDeleted(quint32 id);

public slots:
    /**
     * Create or update a Function from a snapshot emitted by another DocSync.
     * @a type is the type the sender declared, e.g. to check access rights:
     * a snapshot of another type is rejected.
     */
    void applyFunction(quint32 id, int type, const QByteArray &xml);

    /** Delete a Function deleted on another instance */
    void applyFunctionRemoval(quint32 id);

private slots:
    void slotFunctionAdded(quint32 id);
    void slotFunctionChanged(quint32 id);
    void slotFunctionRemoved(quint32 id);
    void slotClearing();
    void slotCleared();

private:
    /** Whether local Doc changes must be collected right now */
    bool isListening() const;

    /** Start the batch timer unless it is already running */
    void scheduleFlush();

    QByteArray functionXML(quint32 id) const;
    void clearPending();

private:
    Doc *m_doc;
    QTimer m_flushTimer;
    bool m_enabled;
    /** True while a remote snapshot is being applied, to avoid echoing it back */
    bool m_applyingRemote;
    /** True between Doc::clearing() and Doc::cleared() */
    bool m_clearing;

    /** Functions created since the last flush, in creation order */
    QList<quint32> m_added;
    QSet<quint32> m_changed;
    QList<quint32> m_removed;

    /** Last snapshot sent or received for each Function, to skip unchanged ones */
    QHash<quint32, QByteArray> m_lastXml;
};

/** @} */

#endif
