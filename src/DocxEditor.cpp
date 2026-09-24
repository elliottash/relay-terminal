// SPDX-License-Identifier: AGPL-3.0-or-later
#include "DocxEditor.h"
#include "AppPaths.h"

#include <QFont>
#include <QEvent>
#include <QFrame>
#include <QColor>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProcess>
#include <QMap>
#include <QScrollArea>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFragment>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace relay {

DocxEditor::DocxEditor(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(8, 4, 8, 4);
    auto button = [this, toolbar](const QString &name, const QString &tip) {
        auto *b = new QToolButton(this);
        b->setText(name);
        b->setToolTip(tip);
        b->setCheckable(true);
        b->setEnabled(false);
        b->setFocusPolicy(Qt::NoFocus);
        toolbar->addWidget(b);
        return b;
    };
    m_bold = button(QStringLiteral("B"), QStringLiteral("Bold (Ctrl+B)"));
    m_bold->setObjectName(QStringLiteral("docxBold"));
    m_italic = button(QStringLiteral("I"), QStringLiteral("Italic (Ctrl+I)"));
    m_italic->setObjectName(QStringLiteral("docxItalic"));
    m_underline = button(QStringLiteral("U"), QStringLiteral("Underline (Ctrl+U)"));
    m_underline->setObjectName(QStringLiteral("docxUnderline"));
    toolbar->addStretch(1);
    auto *scope = new QLabel(QStringLiteral("DOCX · simple paragraphs editable; complex Word content preserved"), this);
    scope->setObjectName(QStringLiteral("docxScope"));
    toolbar->addWidget(scope);
    layout->addLayout(toolbar);
    for (auto [button, which] : {std::pair{m_bold, 0}, std::pair{m_italic, 1}, std::pair{m_underline, 2}}) {
        connect(button, &QToolButton::clicked, this, [this, button, which](bool checked) {
            if (!m_active || m_active->isReadOnly()) return;
            QTextCharFormat fmt;
            if (which == 0) fmt.setFontWeight(checked ? QFont::Bold : QFont::Normal);
            if (which == 1) fmt.setFontItalic(checked);
            if (which == 2) fmt.setFontUnderline(checked);
            m_active->mergeCurrentCharFormat(fmt);
            m_active->setFocus(Qt::OtherFocusReason);
        });
    }
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_page = new QWidget(m_scroll);
    m_paragraphs = new QVBoxLayout(m_page);
    m_paragraphs->setContentsMargins(24, 16, 24, 16);
    m_paragraphs->setSpacing(5);
    m_scroll->setWidget(m_page);
    layout->addWidget(m_scroll, 1);
}

bool DocxEditor::runBridge(const QString &action, const QByteArray &input, QJsonObject *answer, QString *error) {
    const QString python = relayPython();
    if (python.isEmpty()) { *error = QStringLiteral("Python is unavailable"); return false; }
    QProcess process;
    process.start(python, {dataRoot() + QStringLiteral("/backend/relay_core/docx_edit.py"), action, m_path});
    if (!process.waitForStarted(5000)) { *error = process.errorString(); return false; }
    if (!input.isEmpty()) process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(30000)) { process.kill(); *error = QStringLiteral("DOCX processing timed out"); return false; }
    QJsonParseError parseError;
    const QJsonDocument result = QJsonDocument::fromJson(process.readAllStandardOutput(), &parseError);
    if (!result.isObject()) {
        *error = QStringLiteral("DOCX bridge returned invalid data: %1").arg(QString::fromUtf8(process.readAllStandardError()));
        return false;
    }
    *answer = result.object();
    if (process.exitCode() != 0 || answer->contains(QStringLiteral("error"))) {
        *error = answer->value(QStringLiteral("error")).toString(QStringLiteral("DOCX processing failed"));
        return false;
    }
    return true;
}

bool DocxEditor::open(const QString &path, QString *error) {
    m_path = path;
    QJsonObject answer;
    if (!runBridge(QStringLiteral("inspect"), {}, &answer, error)) return false;
    const QJsonArray paragraphs = answer.value(QStringLiteral("paragraphs")).toArray();
    if (paragraphs.size() > 1500) { *error = QStringLiteral("DOCX has too many paragraphs to edit here"); return false; }
    while (QLayoutItem *item = m_paragraphs->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_items.clear();
    m_active = nullptr;
    m_sha = answer.value(QStringLiteral("sha256")).toString();
    QMap<int, QGridLayout *> tables;
    QMap<QString, QVBoxLayout *> cells;
    for (const QJsonValue &value : paragraphs) {
        const QJsonObject row = value.toObject();
        auto *edit = new QTextEdit(m_page);
        edit->setObjectName(QStringLiteral("docxParagraph%1").arg(row.value(QStringLiteral("id")).toInt()));
        // Rich paste can carry colors, fonts and objects this bridge cannot write back. Paste
        // plain text; the toolbar edits the formatting that DOCX save supports.
        edit->setAcceptRichText(false);
        edit->setReadOnly(!row.value(QStringLiteral("editable")).toBool());
        edit->setFrameStyle(QFrame::NoFrame);
        edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        edit->setMinimumHeight(34);
        edit->installEventFilter(this);
        const QString style = row.value(QStringLiteral("style")).toString();
        QFont font = edit->font();
        if (style.startsWith(QStringLiteral("Heading"))) {
            bool ok = false;
            const int level = style.mid(7).toInt(&ok);
            font.setBold(true);
            font.setPointSize(ok ? std::max(12, 22 - 2 * level) : 16);
        } else font.setPointSize(12);
        edit->setFont(font);
        QTextCursor cursor(edit->document());
        for (const QJsonValue &runValue : row.value(QStringLiteral("runs")).toArray()) {
            const QJsonObject run = runValue.toObject();
            QTextCharFormat fmt;
            if (run.value(QStringLiteral("bold")).toBool()) fmt.setFontWeight(QFont::Bold);
            if (run.value(QStringLiteral("italic")).toBool()) fmt.setFontItalic(true);
            if (run.value(QStringLiteral("underline")).toBool()) fmt.setFontUnderline(true);
            const QString family = run.value(QStringLiteral("font")).toString();
            if (!family.isEmpty()) fmt.setFontFamily(family);
            bool sizeOk = false;
            const int halfPoints = run.value(QStringLiteral("size")).toString().toInt(&sizeOk);
            if (sizeOk && halfPoints > 0) fmt.setFontPointSize(halfPoints / 2.0);
            const QString color = run.value(QStringLiteral("color")).toString();
            if (color.size() == 6) {
                const QColor ink(QStringLiteral("#") + color);
                if (ink.isValid()) fmt.setForeground(ink);
            }
            cursor.insertText(run.value(QStringLiteral("text")).toString(), fmt);
        }
        edit->document()->setModified(false);
        if (!row.value(QStringLiteral("editable")).toBool()) {
            edit->setToolTip(QStringLiteral("This paragraph contains Word features Relay preserves but cannot edit here."));
            edit->setStyleSheet(QStringLiteral("QTextEdit { background: rgba(128,128,128,0.08); }"));
        }
        const int height = std::max(34, int(edit->document()->size().height()) + 12);
        edit->setFixedHeight(std::min(height, 500));
        const int table = row.value(QStringLiteral("table")).toInt(-1);
        if (table < 0) {
            m_paragraphs->addWidget(edit);
        } else {
            QGridLayout *grid = tables.value(table, nullptr);
            if (!grid) {
                auto *tableWidget = new QWidget(m_page);
                tableWidget->setObjectName(QStringLiteral("docxTable%1").arg(table));
                grid = new QGridLayout(tableWidget);
                grid->setContentsMargins(0, 0, 0, 0);
                grid->setSpacing(0);
                tables.insert(table, grid);
                m_paragraphs->addWidget(tableWidget);
            }
            const int rowNumber = row.value(QStringLiteral("row")).toInt();
            const int cellNumber = row.value(QStringLiteral("cell")).toInt();
            const QString key = QStringLiteral("%1/%2/%3").arg(table).arg(rowNumber).arg(cellNumber);
            QVBoxLayout *cellLayout = cells.value(key, nullptr);
            if (!cellLayout) {
                auto *cellWidget = new QWidget(m_page);
                cellWidget->setObjectName(QStringLiteral("docxCell%1_%2_%3").arg(table).arg(rowNumber).arg(cellNumber));
                cellWidget->setStyleSheet(QStringLiteral("QWidget#%1 { border: 1px solid palette(mid); background: palette(base); }").arg(cellWidget->objectName()));
                cellLayout = new QVBoxLayout(cellWidget);
                cellLayout->setContentsMargins(3, 2, 3, 2);
                cellLayout->setSpacing(0);
                cells.insert(key, cellLayout);
                grid->addWidget(cellWidget, rowNumber, cellNumber);
            }
            cellLayout->addWidget(edit);
        }
        m_items.append({row.value(QStringLiteral("id")).toInt(), edit, row.value(QStringLiteral("editable")).toBool()});
        connect(edit, &QTextEdit::textChanged, this, [this, edit] {
            if (!edit->isReadOnly()) markDirty();
            edit->setFixedHeight(std::min(500, std::max(34, int(edit->document()->size().height()) + 12)));
        });
        connect(edit, &QTextEdit::cursorPositionChanged, this, [this, edit] { setActive(edit); });
    }
    m_paragraphs->addStretch(1);
    m_dirty = false;
    syncToolbar();
    return true;
}

void DocxEditor::markDirty() {
    if (m_dirty) return;
    m_dirty = true;
    if (onDirtyChanged) onDirtyChanged(true);
}

void DocxEditor::setActive(QTextEdit *editor) {
    m_active = editor;
    syncToolbar();
}

bool DocxEditor::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::FocusIn) {
        if (auto *editor = qobject_cast<QTextEdit *>(watched)) setActive(editor);
    }
    return QWidget::eventFilter(watched, event);
}

void DocxEditor::syncToolbar() {
    const bool enabled = m_active && !m_active->isReadOnly();
    for (QToolButton *button : {m_bold, m_italic, m_underline}) button->setEnabled(enabled);
    if (!enabled) return;
    const QTextCharFormat fmt = m_active->currentCharFormat();
    m_bold->setChecked(fmt.fontWeight() >= QFont::Bold);
    m_italic->setChecked(fmt.fontItalic());
    m_underline->setChecked(fmt.fontUnderline());
}

QJsonArray DocxEditor::runsFor(const QTextEdit *editor) const {
    QJsonArray runs;
    bool firstBlock = true;
    for (QTextBlock block = editor->document()->begin(); block.isValid(); block = block.next()) {
        if (!firstBlock) runs.append(QJsonObject{{QStringLiteral("text"), QStringLiteral("\n")}});
        firstBlock = false;
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) continue;
            const QTextCharFormat fmt = fragment.charFormat();
            runs.append(QJsonObject{{QStringLiteral("text"), fragment.text()},
                                    {QStringLiteral("bold"), fmt.fontWeight() >= QFont::Bold},
                                    {QStringLiteral("italic"), fmt.fontItalic()},
                                    {QStringLiteral("underline"), fmt.fontUnderline()}});
        }
    }
    return runs;
}

bool DocxEditor::save(QString *error) {
    if (!m_dirty) return true;
    QJsonArray edits;
    for (const Paragraph &item : m_items) {
        if (!item.editable || !item.editor->document()->isModified()) continue;
        edits.append(QJsonObject{{QStringLiteral("id"), item.id}, {QStringLiteral("runs"), runsFor(item.editor)}});
    }
    QJsonObject answer;
    const QByteArray request = QJsonDocument(QJsonObject{{QStringLiteral("sha256"), m_sha},
                                                        {QStringLiteral("edits"), edits}}).toJson(QJsonDocument::Compact);
    if (!runBridge(QStringLiteral("save"), request, &answer, error)) return false;
    m_sha = answer.value(QStringLiteral("sha256")).toString();
    for (const Paragraph &item : m_items) item.editor->document()->setModified(false);
    m_dirty = false;
    if (onDirtyChanged) onDirtyChanged(false);
    return true;
}

QString DocxEditor::plainText() const {
    QStringList paragraphs;
    for (const Paragraph &item : m_items) paragraphs.append(item.editor->toPlainText());
    return paragraphs.join(QLatin1Char('\n'));
}

}  // namespace relay
