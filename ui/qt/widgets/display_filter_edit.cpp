/* display_filter_edit.cpp
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <glib.h>

#include <epan/dfilter/dfilter.h>

#include <ui/filter_files.h>

#include <wsutil/utf8_entities.h>

#include <ui/qt/widgets/display_filter_edit.h>
#include "filter_dialog.h"
#include <ui/qt/widgets/stock_icon_tool_button.h>
#include <ui/qt/widgets/syntax_line_edit.h>
#include <ui/qt/utils/wireshark_mime_data.h>
#include <ui/qt/models/pref_models.h>
#include "wireshark_application.h"

#include <QAction>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCompleter>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QStringListModel>
#include <QWidget>
#include <QObject>
#include <QDrag>
#include <QDropEvent>
#include <QMimeData>

// To do:
// - Get rid of shortcuts and replace them with "n most recently applied filters"?
// - We need simplified (button- and dropdown-free) versions for use in dialogs and field-only checking.
// - Add a separator or otherwise distinguish between recent items and fields
//   in the completion dropdown.

#ifdef __APPLE__
#define DEFAULT_MODIFIER UTF8_PLACE_OF_INTEREST_SIGN
#else
#define DEFAULT_MODIFIER "Ctrl-"
#endif

// proto.c:fld_abbrev_chars
static const QString fld_abbrev_chars_ = "-.0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz";

DisplayFilterEdit::DisplayFilterEdit(QWidget *parent, DisplayFilterEditType type) :
    SyntaxLineEdit(parent),
    type_(type),
    save_action_(NULL),
    remove_action_(NULL),
    bookmark_button_(NULL),
    clear_button_(NULL),
    apply_button_(NULL)
{
    setAccessibleName(tr("Display filter entry"));

    completion_model_ = new QStringListModel(this);
    setCompleter(new QCompleter(completion_model_, this));
    setCompletionTokenChars(fld_abbrev_chars_);

    setDefaultPlaceholderText();

    //   DFCombo
    //     Bookmark
    //     DisplayFilterEdit
    //     Clear button
    //     Apply (right arrow)
    //     Combo drop-down

    if (type_ == DisplayFilterToApply) {
        bookmark_button_ = new StockIconToolButton(this, "x-display-filter-bookmark");
        bookmark_button_->setCursor(Qt::ArrowCursor);
        bookmark_button_->setMenu(new QMenu(bookmark_button_));
        bookmark_button_->setPopupMode(QToolButton::InstantPopup);
        bookmark_button_->setToolTip(tr("Manage saved bookmarks."));
        bookmark_button_->setIconSize(QSize(14, 14));
        bookmark_button_->setStyleSheet(
                "QToolButton {"
                "  border: none;"
                "  background: transparent;" // Disables platform style on Windows.
                "  padding: 0 0 0 0;"
                "}"
                "QToolButton::menu-indicator { image: none; }"
                );
    }

    if (type_ == DisplayFilterToApply) {
        clear_button_ = new StockIconToolButton(this, "x-filter-clear");
        clear_button_->setCursor(Qt::ArrowCursor);
        clear_button_->setToolTip(QString());
        clear_button_->setIconSize(QSize(14, 14));
        clear_button_->setStyleSheet(
                "QToolButton {"
                "  border: none;"
                "  background: transparent;" // Disables platform style on Windows.
                "  padding: 0 0 0 0;"
                "  margin-left: 1px;"
                "}"
                );
        connect(clear_button_, &StockIconToolButton::clicked, this, &DisplayFilterEdit::clearFilter);
    }

    connect(this, &DisplayFilterEdit::textChanged, this,
            static_cast<void (DisplayFilterEdit::*)(const QString &)>(&DisplayFilterEdit::checkFilter));

    if (type_ == DisplayFilterToApply) {
        apply_button_ = new StockIconToolButton(this, "x-filter-apply");
        apply_button_->setCursor(Qt::ArrowCursor);
        apply_button_->setEnabled(false);
        apply_button_->setToolTip(tr("Apply this filter string to the display."));
        apply_button_->setIconSize(QSize(24, 14));
        apply_button_->setStyleSheet(
                "QToolButton {"
                "  border: none;"
                "  background: transparent;" // Disables platform style on Windows.
                "  padding: 0 0 0 0;"
                "}"
                );
        connect(apply_button_, &StockIconToolButton::clicked, this, &DisplayFilterEdit::applyDisplayFilter);
        connect(this, &DisplayFilterEdit::returnPressed, this, &DisplayFilterEdit::applyDisplayFilter);
    }

    int frameWidth = style()->pixelMetric(QStyle::PM_DefaultFrameWidth);
    QSize bksz;
    if (bookmark_button_) {
        bksz = bookmark_button_->sizeHint();
    }
    QSize cbsz;
    if (clear_button_) {
        cbsz = clear_button_->sizeHint();
    }
    QSize apsz;
    if (apply_button_) {
        apsz = apply_button_->sizeHint();
    }
    setStyleSheet(QString(
            "DisplayFilterEdit {"
            "  padding-left: %1px;"
            "  margin-left: %2px;"
            "  margin-right: %3px;"
            "}"
            )
            .arg(frameWidth + 1)
            .arg(bksz.width())
            .arg(cbsz.width() + apsz.width() + frameWidth + 1)
                  );

    connect(wsApp, &WiresharkApplication::appInitialized, this, &DisplayFilterEdit::updateBookmarkMenu);
    connect(wsApp, &WiresharkApplication::displayFilterListChanged, this, &DisplayFilterEdit::updateBookmarkMenu);

}

void DisplayFilterEdit::setDefaultPlaceholderText()
{
    switch (type_) {

    case DisplayFilterToApply:
        placeholder_text_ = QString(tr("Apply a display filter %1 <%2/>")).arg(UTF8_HORIZONTAL_ELLIPSIS)
    .arg(DEFAULT_MODIFIER);
        break;

    case DisplayFilterToEnter:
        placeholder_text_ = QString(tr("Enter a display filter %1")).arg(UTF8_HORIZONTAL_ELLIPSIS);
        break;

    case ReadFilterToApply:
        placeholder_text_ = QString(tr("Apply a read filter %1")).arg(UTF8_HORIZONTAL_ELLIPSIS);
        break;
    }
    setPlaceholderText(placeholder_text_);
}

void DisplayFilterEdit::paintEvent(QPaintEvent *evt) {
    SyntaxLineEdit::paintEvent(evt);

    if (bookmark_button_) {
        // Draw the right border by hand. We could try to do this in the
        // style sheet but it's a pain.
#ifdef Q_OS_MAC
        QColor divider_color = Qt::gray;
#else
        QColor divider_color = palette().shadow().color();
#endif
        QPainter painter(this);
        painter.setPen(divider_color);
        QRect cr = contentsRect();
        QSize bksz = bookmark_button_->size();
        painter.drawLine(bksz.width(), cr.top(), bksz.width(), cr.bottom());
    }
}

void DisplayFilterEdit::resizeEvent(QResizeEvent *)
{
    QSize cbsz;
    if (clear_button_) {
        cbsz = clear_button_->sizeHint();
    }
    QSize apsz;
    if (apply_button_) {
        apsz = apply_button_->sizeHint();
    } else {
        apsz.setHeight(0); apsz.setWidth(0);
    }
    int frameWidth = style()->pixelMetric(QStyle::PM_DefaultFrameWidth);
    if (clear_button_) {
        clear_button_->move(contentsRect().right() - frameWidth - cbsz.width() - apsz.width(),
                            contentsRect().top());
        clear_button_->setMinimumHeight(contentsRect().height());
        clear_button_->setMaximumHeight(contentsRect().height());
    }
    if (apply_button_) {
        apply_button_->move(contentsRect().right() - frameWidth - apsz.width(),
                            contentsRect().top());
        apply_button_->setMinimumHeight(contentsRect().height());
        apply_button_->setMaximumHeight(contentsRect().height());
    }
    if (bookmark_button_) {
        bookmark_button_->setMinimumHeight(contentsRect().height());
        bookmark_button_->setMaximumHeight(contentsRect().height());
    }
}

void DisplayFilterEdit::focusOutEvent(QFocusEvent *event)
{
    if (syntaxState() == Valid) {
        emit popFilterSyntaxStatus();
        setToolTip(QString());
    }
    SyntaxLineEdit::focusOutEvent(event);
}

bool DisplayFilterEdit::checkFilter()
{
    checkFilter(text());

    return syntaxState() != Invalid;
}

void DisplayFilterEdit::checkFilter(const QString& filter_text)
{
    if (clear_button_) {
        clear_button_->setVisible(!filter_text.isEmpty());
    }

    emit popFilterSyntaxStatus();
    setToolTip(QString());
    checkDisplayFilter(filter_text);

    switch (syntaxState()) {
    case Deprecated:
    {
        emit pushFilterSyntaxStatus(syntaxErrorMessage());
        setToolTip(syntaxErrorMessage());
        break;
    }
    case Invalid:
    {
        QString invalidMsg(tr("Invalid filter: "));
        invalidMsg.append(syntaxErrorMessage());
        emit pushFilterSyntaxStatus(invalidMsg);
        setToolTip(invalidMsg);
        break;
    }
    default:
        break;
    }

    if (bookmark_button_) {
        bool enable_save_action = false;
        bool match = false;

        for (GList *df_item = get_filter_list_first(DFILTER_LIST); df_item; df_item = g_list_next(df_item)) {
            if (!df_item->data) continue;
            filter_def *df_def = (filter_def *) df_item->data;
            if (!df_def->name || !df_def->strval) continue;

            if (filter_text.compare(df_def->strval) == 0) {
                match = true;
            }
        }

        if (match) {
            bookmark_button_->setStockIcon("x-filter-matching-bookmark");
            if (remove_action_) {
                remove_action_->setData(text());
                remove_action_->setVisible(true);
            }
        } else {
            bookmark_button_->setStockIcon("x-display-filter-bookmark");
            if (remove_action_) {
                remove_action_->setVisible(false);
            }
        }

        if (!match && (syntaxState() == Valid || syntaxState() == Deprecated) && !filter_text.isEmpty()) {
            enable_save_action = true;
        }
        if (save_action_) {
            save_action_->setEnabled(enable_save_action);
        }
    }
    if (apply_button_) {
        apply_button_->setEnabled(syntaxState() != Invalid);
    }
}

void DisplayFilterEdit::updateBookmarkMenu()
{
    if (!bookmark_button_)
        return;

    QMenu *bb_menu = bookmark_button_->menu();
    bb_menu->clear();

    save_action_ = bb_menu->addAction(tr("Save this filter"));
    connect(save_action_, &QAction::triggered, this, &DisplayFilterEdit::saveFilter);
    remove_action_ = bb_menu->addAction(tr("Remove this filter"));
    connect(remove_action_, &QAction::triggered, this, &DisplayFilterEdit::removeFilter);
    QAction *manage_action = bb_menu->addAction(tr("Manage Display Filters"));
    connect(manage_action, &QAction::triggered, this, &DisplayFilterEdit::showFilters);
    QAction *expr_action = bb_menu->addAction(tr("Manage Filter Expressions"));
    connect(expr_action, &QAction::triggered, this, &DisplayFilterEdit::showExpressionPrefs);
    bb_menu->addSeparator();

    for (GList *df_item = get_filter_list_first(DFILTER_LIST); df_item; df_item = g_list_next(df_item)) {
        if (!df_item->data) continue;
        filter_def *df_def = (filter_def *) df_item->data;
        if (!df_def->name || !df_def->strval) continue;

        int one_em = bb_menu->fontMetrics().height();
        QString prep_text = QString("%1: %2").arg(df_def->name).arg(df_def->strval);
        prep_text = bb_menu->fontMetrics().elidedText(prep_text, Qt::ElideRight, one_em * 40);

        QAction *prep_action = bb_menu->addAction(prep_text);
        prep_action->setData(df_def->strval);
        connect(prep_action, &QAction::triggered, this, &DisplayFilterEdit::applyOrPrepareFilter);
    }

    checkFilter();
}

// GTK+ behavior:
// - Operates on words (proto.c:fld_abbrev_chars).
// - Popup appears when you enter or remove text.

// Our behavior:
// - Operates on words (fld_abbrev_chars_).
// - Popup appears when you enter or remove text.
// - Popup appears when you move the cursor.
// - Popup does not appear when text is selected.
// - Recent and saved display filters in popup when editing first word.

// ui/gtk/filter_autocomplete.c:build_autocompletion_list
void DisplayFilterEdit::buildCompletionList(const QString &field_word)
{
    // Push a hint about the current field.
    if (syntaxState() == Valid) {
        emit popFilterSyntaxStatus();

        header_field_info *hfinfo = proto_registrar_get_byname(field_word.toUtf8().constData());
        if (hfinfo) {
            QString cursor_field_msg = QString("%1: %2")
                    .arg(hfinfo->name)
                    .arg(ftype_pretty_name(hfinfo->type));
            emit pushFilterSyntaxStatus(cursor_field_msg);
        }
    }

    if (field_word.length() < 1) {
        completion_model_->setStringList(QStringList());
        return;
    }

    // Grab matching display filters from our parent combo and from the
    // saved display filters file. Skip ones that look like single fields
    // and assume they will be added below.
    QStringList complex_list;
    QComboBox *df_combo = qobject_cast<QComboBox *>(parent());
    if (df_combo) {
        for (int i = 0; i < df_combo->count() ; i++) {
            QString recent_filter = df_combo->itemText(i);

            if (isComplexFilter(recent_filter)) {
                complex_list << recent_filter;
            }
        }
    }
    for (const GList *df_item = get_filter_list_first(DFILTER_LIST); df_item; df_item = g_list_next(df_item)) {
        const filter_def *df_def = (filter_def *) df_item->data;
        if (!df_def || !df_def->strval) continue;
        QString saved_filter = df_def->strval;

        if (isComplexFilter(saved_filter) && !complex_list.contains(saved_filter)) {
            complex_list << saved_filter;
        }
    }
    completion_model_->setStringList(complex_list);
    completer()->setCompletionPrefix(field_word);

    void *proto_cookie;
    QStringList field_list;
    int field_dots = field_word.count('.'); // Some protocol names (_ws.expert) contain periods.
    for (int proto_id = proto_get_first_protocol(&proto_cookie); proto_id != -1; proto_id = proto_get_next_protocol(&proto_cookie)) {
        protocol_t *protocol = find_protocol_by_id(proto_id);
        if (!proto_is_protocol_enabled(protocol)) continue;

        const QString pfname = proto_get_protocol_filter_name(proto_id);
        field_list << pfname;

        // Add fields only if we're past the protocol name and only for the
        // current protocol.
        if (field_dots > pfname.count('.')) {
            void *field_cookie;
            const QByteArray fw_ba = field_word.toUtf8(); // or toLatin1 or toStdString?
            const char *fw_utf8 = fw_ba.constData();
            gsize fw_len = (gsize) strlen(fw_utf8);
            for (header_field_info *hfinfo = proto_get_first_protocol_field(proto_id, &field_cookie); hfinfo; hfinfo = proto_get_next_protocol_field(proto_id, &field_cookie)) {
                if (hfinfo->same_name_prev_id != -1) continue; // Ignore duplicate names.

                if (!g_ascii_strncasecmp(fw_utf8, hfinfo->abbrev, fw_len)) {
                    if ((gsize) strlen(hfinfo->abbrev) != fw_len) field_list << hfinfo->abbrev;
                }
            }
        }
    }
    field_list.sort();

    completion_model_->setStringList(complex_list + field_list);
    completer()->setCompletionPrefix(field_word);
}

void DisplayFilterEdit::clearFilter()
{
    clear();
    QString new_filter;
    emit filterPackets(new_filter, true);
}

void DisplayFilterEdit::applyDisplayFilter()
{
    if (syntaxState() == Invalid) {
        return;
    }

    QString new_filter = text();
    emit filterPackets(new_filter, true);
}

void DisplayFilterEdit::displayFilterSuccess(bool success)
{
    apply_button_->setEnabled(!success);
}

void DisplayFilterEdit::changeEvent(QEvent* event)
{
    if (0 != event)
    {
        switch (event->type())
        {
        case QEvent::LanguageChange:
            setDefaultPlaceholderText();
            break;
        default:
            break;
        }
    }
    SyntaxLineEdit::changeEvent(event);
}

void DisplayFilterEdit::saveFilter()
{
    FilterDialog display_filter_dlg(window(), FilterDialog::DisplayFilter, text());
    display_filter_dlg.exec();
}

void DisplayFilterEdit::removeFilter()
{
    QAction *ra = qobject_cast<QAction*>(sender());
    if (!ra || ra->data().toString().isEmpty()) return;

    QString remove_filter = ra->data().toString();

    for (GList *df_item = get_filter_list_first(DFILTER_LIST); df_item; df_item = g_list_next(df_item)) {
        if (!df_item->data) continue;
        filter_def *df_def = (filter_def *) df_item->data;
        if (!df_def->name || !df_def->strval) continue;

        if (remove_filter.compare(df_def->strval) == 0) {
            remove_from_filter_list(DFILTER_LIST, df_item);
        }
    }

    save_filter_list(DFILTER_LIST);

    updateBookmarkMenu();
}

void DisplayFilterEdit::showFilters()
{
    FilterDialog display_filter_dlg(window(), FilterDialog::DisplayFilter);
    display_filter_dlg.exec();
}

void DisplayFilterEdit::showExpressionPrefs()
{
    emit showPreferencesDialog(PrefsModel::FILTER_BUTTONS_PREFERENCE_TREE_NAME);
}

void DisplayFilterEdit::applyOrPrepareFilter()
{
    QAction *pa = qobject_cast<QAction*>(sender());
    if (!pa || pa->data().toString().isEmpty()) return;

    setText(pa->data().toString());

    // Holding down the Shift key will only prepare filter.
    if (!(QApplication::keyboardModifiers() & Qt::ShiftModifier)) {
        applyDisplayFilter();
    }
}

void DisplayFilterEdit::dragEnterEvent(QDragEnterEvent *event)
{
    if ( ! event )
        return;

    if (qobject_cast<const DisplayFilterMimeData *>(event->mimeData())) {
        if ( event->source() != this )
        {
            event->setDropAction(Qt::CopyAction);
            event->accept();
        } else {
            event->acceptProposedAction();
        }
    } else {
        event->ignore();
    }
}

void DisplayFilterEdit::dragMoveEvent(QDragMoveEvent *event)
{
    if ( ! event )
        return;

    if (qobject_cast<const DisplayFilterMimeData *>(event->mimeData())) {
        if ( event->source() != this )
        {
            event->setDropAction(Qt::CopyAction);
            event->accept();
        } else {
            event->acceptProposedAction();
        }
    } else {
        event->ignore();
    }
}

void DisplayFilterEdit::dropEvent(QDropEvent *event)
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

            QString filterText;
            if ((QApplication::keyboardModifiers() & Qt::AltModifier))
                filterText = data->field();
            else
                filterText = data->filter();

            if ( text().length() > 0 && QApplication::keyboardModifiers() & Qt::MetaModifier)
            {
                createFilterTextDropMenu(event, filterText);
                return;
            }

            setText(filterText);

            // Holding down the Shift key will only prepare filter.
            if (!(QApplication::keyboardModifiers() & Qt::ShiftModifier)) {
                applyDisplayFilter();
            }

        } else {
            event->acceptProposedAction();
        }

    } else {
        event->ignore();
    }
}

void DisplayFilterEdit::createFilterTextDropMenu(QDropEvent *event, QString filterText)
{
    if ( filterText.isEmpty() )
        return;

    QMenu applyMenu(this);

    QAction * andAction = new QAction(tr("...and selected"));
    andAction->setData(QString("&& %1").arg(filterText));
    connect(andAction, &QAction::triggered, this, &DisplayFilterEdit::dropActionMenuEvent);

    QAction * orAction = new QAction(tr("...or selected"));
    orAction->setData(QString("|| %1").arg(filterText));
    connect(orAction, &QAction::triggered, this, &DisplayFilterEdit::dropActionMenuEvent);

    QAction * andNotAction = new QAction(tr("...and not selected"));
    andNotAction->setData(QString("&& !(%1)").arg(filterText));
    connect(andNotAction, &QAction::triggered, this, &DisplayFilterEdit::dropActionMenuEvent);

    QAction * orNotAction = new QAction(tr("...or not selected"));
    orNotAction->setData(QString("|| !(%1)").arg(filterText));
    connect(orNotAction, &QAction::triggered, this, &DisplayFilterEdit::dropActionMenuEvent);

    applyMenu.addAction(andAction);
    applyMenu.addAction(orAction);
    applyMenu.addAction(andNotAction);
    applyMenu.addAction(orNotAction);
    applyMenu.exec(this->mapToGlobal(event->pos()));

}

void DisplayFilterEdit::dropActionMenuEvent()
{
    QAction * sendAction = qobject_cast<QAction *>(sender());
    if ( ! sendAction )
        return;

    QString value = sendAction->data().toString();

    QString filterText = QString("((%1) %2)").arg(this->text()).arg(value);
    setText(filterText);

    // Holding down the Shift key will only prepare filter.
    if (!(QApplication::keyboardModifiers() & Qt::ShiftModifier)) {
        applyDisplayFilter();
    }
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
