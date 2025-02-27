/* packet_list_header.cpp
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QDropEvent>
#include <QMimeData>
#include <QToolTip>
#include <QAction>
#include <QInputDialog>

#include <packet_list.h>

#include <wireshark_application.h>
#include <epan/column.h>
#include <ui/recent.h>
#include <ui/preference_utils.h>
#include <ui/packet_list_utils.h>
#include <ui/qt/main_window.h>

#include <models/packet_list_model.h>
#include <ui/qt/utils/wireshark_mime_data.h>
#include <ui/qt/widgets/packet_list_header.h>

PacketListHeader::PacketListHeader(Qt::Orientation orientation, capture_file * cap_file, QWidget *parent) :
    QHeaderView(orientation, parent),
    cap_file_(cap_file)
{
    setAcceptDrops(true);
    setSectionsMovable(true);
    setStretchLastSection(true);
    setDefaultAlignment(Qt::AlignLeft|Qt::AlignVCenter);
}

void PacketListHeader::dragEnterEvent(QDragEnterEvent *event)
{
    if ( ! event )
        return;

    if (qobject_cast<const DisplayFilterMimeData *>(event->mimeData()))
    {
        if ( event->source() != this )
        {
            event->setDropAction(Qt::CopyAction);
            event->accept();
        } else {
            event->acceptProposedAction();
        }
    }
    else
        QHeaderView::dragEnterEvent(event);
}

void PacketListHeader::dragMoveEvent(QDragMoveEvent *event)
{
    if ( ! event )
        return;

    if (qobject_cast<const DisplayFilterMimeData *>(event->mimeData()))
    {
        if ( event->source() != this )
        {
            event->setDropAction(Qt::CopyAction);
            event->accept();
        } else {
            event->acceptProposedAction();
        }
    }
    else
        QHeaderView::dragMoveEvent(event);
}

void PacketListHeader::dropEvent(QDropEvent *event)
{
    if ( ! event )
        return;

    /* Moving items around */
    if (qobject_cast<const DisplayFilterMimeData *>(event->mimeData())) {
        const DisplayFilterMimeData * data = qobject_cast<const DisplayFilterMimeData *>(event->mimeData());

        if ( event->source() != this )
        {
            event->setDropAction(Qt::CopyAction);
            event->accept();

            MainWindow * mw = qobject_cast<MainWindow *>(wsApp->mainWindow());
            if ( mw )
            {
                int idx = logicalIndexAt(event->pos());
                mw->insertColumn(data->description(), data->field(), idx);
            }

        } else {
            event->acceptProposedAction();
        }
    }
    else
        QHeaderView::dropEvent(event);
}

void PacketListHeader::mousePressEvent(QMouseEvent *e)
{
    if ( e->button() == Qt::LeftButton && sectionIdx < 0 )
    {
        /* No move happening yet */
        int sectIdx = logicalIndexAt(e->localPos().x() - 4, e->localPos().y());

        QString headerName = model()->headerData(sectIdx, orientation()).toString();
        lastSize = sectionSize(sectIdx);
        QToolTip::showText(e->globalPos(), QString("Width: %1").arg(sectionSize(sectIdx)));
    }
    QHeaderView::mousePressEvent(e);
}

void PacketListHeader::mouseMoveEvent(QMouseEvent *e)
{
    if ( e->button() == Qt::NoButton || ! ( e->buttons() & Qt::LeftButton) )
    {
        /* no move is happening */
        sectionIdx = -1;
        lastSize = -1;
    }
    else if ( e->buttons() & Qt::LeftButton )
    {
        /* section being moved */
        int triggeredSection = logicalIndexAt(e->localPos().x() - 4, e->localPos().y());

        if ( sectionIdx < 0 )
            sectionIdx = triggeredSection;
        else if ( sectionIdx == triggeredSection )
        {
            /* Only run for the current moving section after a change */
            QString headerName = model()->headerData(sectionIdx, orientation()).toString();
            lastSize = sectionSize(sectionIdx);
            QToolTip::showText(e->globalPos(), QString("Width: %1").arg(lastSize));
        }
    }
    QHeaderView::mouseMoveEvent(e);
}

void PacketListHeader::setCaptureFile(capture_file *cap_file)
{
    this->cap_file_ = cap_file;
}

void PacketListHeader::contextMenuEvent(QContextMenuEvent *event)
{
    int sectionIdx = logicalIndexAt(event->pos());
    QAction * action = Q_NULLPTR;
    QMenu * contextMenu = new QMenu(this);
    contextMenu->setProperty("column", qVariantFromValue(sectionIdx));

    QActionGroup * alignmentActions = new QActionGroup(contextMenu);
    alignmentActions->setExclusive(true);
    alignmentActions->setProperty("column", qVariantFromValue(sectionIdx));
    action = alignmentActions->addAction(tr("Align Left"));
    action->setCheckable(true);
    action->setChecked(false);
    if ( recent_get_column_xalign(sectionIdx) == COLUMN_XALIGN_LEFT || recent_get_column_xalign(sectionIdx) == COLUMN_XALIGN_DEFAULT )
        action->setChecked(true);
    action->setData(qVariantFromValue(COLUMN_XALIGN_LEFT));
    action = alignmentActions->addAction(tr("Align Center"));
    action->setCheckable(true);
    action->setChecked(recent_get_column_xalign(sectionIdx) == COLUMN_XALIGN_CENTER ? true : false);
    action->setData(qVariantFromValue(COLUMN_XALIGN_CENTER));
    action = alignmentActions->addAction(tr("Align Right"));
    action->setCheckable(true);
    action->setChecked(recent_get_column_xalign(sectionIdx) == COLUMN_XALIGN_RIGHT ? true : false);
    action->setData(qVariantFromValue(COLUMN_XALIGN_RIGHT));
    connect(alignmentActions, &QActionGroup::triggered, this, &PacketListHeader::setAlignment);

    contextMenu->addActions(alignmentActions->actions());
    contextMenu->addSeparator();

    action = contextMenu->addAction(tr("Column Preferences" UTF8_HORIZONTAL_ELLIPSIS));
    connect(action, &QAction::triggered, this, &PacketListHeader::showColumnPrefs);
    action = contextMenu->addAction(tr("Edit Column"));
    connect(action, &QAction::triggered, this, &PacketListHeader::doEditColumn);
    action = contextMenu->addAction(tr("Resize to Contents"));
    connect(action, &QAction::triggered, this, &PacketListHeader::resizeToContent);
    action = contextMenu->addAction(tr("Resize Column to Width ..."));
    connect(action, &QAction::triggered, this, &PacketListHeader::resizeToWidth);

    action = contextMenu->addAction(tr("Resolve Names"));
    bool canResolve = resolve_column(sectionIdx, cap_file_);
    action->setEnabled(canResolve);
    action->setChecked(canResolve && get_column_resolved(sectionIdx));
    connect(action, &QAction::triggered, this, &PacketListHeader::doResolveNames);

    contextMenu->addSeparator();

    for (int cnt = 0; cnt < prefs.num_cols; cnt++) {
        QAction *action = new QAction(get_column_title(cnt));
        action->setCheckable(true);
        action->setChecked(get_column_visible(cnt));
        action->setData(QVariant::fromValue(cnt));
        connect(action, &QAction::triggered, this, &PacketListHeader::columnVisibilityTriggered);
        contextMenu->addAction(action);
    }

    contextMenu->addSeparator();

    action = contextMenu->addAction(tr("Remove this Column"));
    action->setEnabled(sectionIdx >= 0 && count() > 2);
    connect(action, &QAction::triggered, this, &PacketListHeader::removeColumn);

    contextMenu->popup(viewport()->mapToGlobal(event->pos()));
}

void PacketListHeader::setSectionVisibility()
{
    for (int cnt = 0; cnt < prefs.num_cols; cnt++)
        setSectionHidden(cnt, get_column_visible(cnt) ? false : true);
}

void PacketListHeader::columnVisibilityTriggered()
{
    QAction *ha = qobject_cast<QAction*>(sender());
    if (!ha) return;

    int col = ha->data().toInt();
    set_column_visible(col, ha->isChecked());
    setSectionVisibility();
    if (ha->isChecked())
        emit resetColumnWidth(col);

    prefs_main_write();
}

void PacketListHeader::setAlignment(QAction *action)
{
    if (!action)
        return;

    QActionGroup * group = action->actionGroup();
    if (! group)
        return;

    int section = group->property("column").toInt();
    if ( section >= 0 )
    {
        QChar data = action->data().toChar();
        recent_set_column_xalign(section, action->isChecked() ? data.toLatin1() : COLUMN_XALIGN_DEFAULT);
        emit updatePackets(false);
    }
}

void PacketListHeader::showColumnPrefs()
{
    emit showColumnPreferences(PrefsModel::COLUMNS_PREFERENCE_TREE_NAME);
}

void PacketListHeader::doEditColumn()
{
    QAction * action = qobject_cast<QAction *>(sender());
    if (!action)
        return;

    QMenu * menu = qobject_cast<QMenu *>(action->parent());
    if (! menu)
        return;

    int section = menu->property("column").toInt();
    emit editColumn(section);
}

void PacketListHeader::doResolveNames()
{
    QAction * action = qobject_cast<QAction *>(sender());
    QMenu * menu = qobject_cast<QMenu *>(action->parent());
    PacketListModel * plmModel = qobject_cast<PacketListModel *>(model());
    if ( ! action || ! menu || ! plmModel )
        return;

    int section = menu->property("column").toInt();

    set_column_resolved(section, action->isChecked());
    plmModel->resetColumns();
    prefs_main_write();
    emit updatePackets(true);
}

void PacketListHeader::resizeToContent()
{
    QAction * action = qobject_cast<QAction *>(sender());
    QMenu * menu = qobject_cast<QMenu *>(action->parent());
    if (! action || ! menu)
        return;

    int section = menu->property("column").toInt();
    PacketList * packetList = qobject_cast<PacketList *>(parent());
    if ( packetList )
        packetList->resizeColumnToContents(section);
}

void PacketListHeader::removeColumn()
{
    QAction * action = qobject_cast<QAction *>(sender());
    QMenu * menu = qobject_cast<QMenu *>(action->parent());
    if (! action || ! menu)
        return;

    int section = menu->property("column").toInt();

    if (count() > 2) {
        column_prefs_remove_nth(section);
        emit columnsChanged();
        prefs_main_write();
    }
}

void PacketListHeader::resizeToWidth()
{
    QAction * action = qobject_cast<QAction *>(sender());
    QMenu * menu = qobject_cast<QMenu *>(action->parent());
    if (! action || ! menu)
        return;

    bool ok = false;
    int width = -1;
    int section = menu->property("column").toInt();
    QString headerName = model()->headerData(section, orientation()).toString();
    width = QInputDialog::getInt(this, tr("Column %1").arg(headerName), tr("Width:"),
                                 sectionSize(section), 0, 1000, 1, &ok);
    if (ok)
        resizeSection(section, width);
}

/*
 * Editor modelines
 *
 * Local Variables:
 * c-basic-offset: 4
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * ex: set shiftwidth=4 tabstop=8 expandtab:
 * :indentSize=4:tabSize=8:noTabs=true:
 */
