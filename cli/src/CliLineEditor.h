#ifndef CLILINEEDITOR_H
#define CLILINEEDITOR_H

#include <functional>

#include <QString>
#include <QTextStream>

#include "CliCompletion.h"

class CliHistory;

/**
 * @brief Blocking interactive line editor with history and Tab completion.
 *
 * Uses raw console input when stdin is a TTY. Otherwise falls back to QTextStream::readLine()
 * without history navigation or completion.
 */
class CliLineEditor
{
public:
    enum class Status {
        Line,
        Interrupted,
        Eof,
    };

    using CompletionHandler = std::function<CliCompletion::Result(const QString &line, int cursor)>;
    using InterruptCheck = std::function<bool()>;

    CliLineEditor();
    ~CliLineEditor();

    CliLineEditor(const CliLineEditor &) = delete;
    CliLineEditor &operator=(const CliLineEditor &) = delete;
    CliLineEditor(CliLineEditor &&) = delete;
    CliLineEditor &operator=(CliLineEditor &&) = delete;

    void setOutput(QTextStream *output);
    void setFallbackInput(QTextStream *input);
    void setPrompt(const QString &prompt, int visibleWidth);
    void setHistory(CliHistory *history);
    void setCompletionHandler(CompletionHandler handler);
    void setInterruptCheck(InterruptCheck check);

    Status readLine(QString *line);

private:
    enum class Key {
        None,
        Enter,
        Backspace,
        Delete,
        Left,
        Right,
        Home,
        End,
        Up,
        Down,
        Tab,
        Interrupt,
        Eof,
        Character,
    };

    struct KeyEvent
    {
        Key key;
        QChar character;
    };

    [[nodiscard]] bool isInteractive() const;
    [[nodiscard]] bool interruptRequested();
    Status readFallbackLine(QString *line);
    Status readRawLine(QString *line);
    bool readKey(KeyEvent *event);
    void insertText(const QString &text);
    void deleteBeforeCursor();
    void deleteAtCursor();
    void moveLeft();
    void moveRight();
    void moveHome();
    void moveEnd();
    void historyUp();
    void historyDown();
    void complete();
    void applyCompletion(const CliCompletion::Result &completion);
    void redraw() const;
    void write(const QString &text) const;
    void flush() const;

    QTextStream *m_output;
    QTextStream *m_fallbackInput;
    CliHistory *m_history;
    CompletionHandler m_completionHandler;
    InterruptCheck m_interruptCheck;
    QString m_prompt;
    int m_promptVisibleWidth;
    QString m_buffer;
    int m_cursor;
};

#endif // CLILINEEDITOR_H
