// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ProgramCompletion.h"

#include <QFileInfo>

namespace relay {
namespace {
QStringList words(const char *table) {
    return QString::fromLatin1(table).split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

QStringList vocabulary(const QString &program) {
    const QString name = QFileInfo(program).fileName().toLower();
    if (name == QLatin1String("python") || name == QLatin1String("python3")
        || name.startsWith(QLatin1String("python3.")) || name == QLatin1String("ipython")) {
        QStringList result = words("False None True and as assert async await break class continue def del elif else except finally for from global if import in is lambda nonlocal not or pass raise return try while with yield "
                                   "abs all any ascii bin bool breakpoint bytearray bytes callable chr classmethod compile complex delattr dict dir divmod enumerate eval exec filter float format frozenset getattr globals hasattr hash help hex id input int isinstance issubclass iter len list locals map max memoryview min next object oct open ord pow print property range repr reversed round set setattr slice sorted staticmethod str sum super tuple type vars zip __import__");
        if (name == QLatin1String("ipython"))
            result += words("%alias %autoawait %autocall %automagic %bookmark %cd %clear %config %debug %dhist %dirs %edit %env %history %load %lsmagic %matplotlib %mkdir %more %notebook %paste %pdb %pdef %pdoc %pinfo %pip %prun %pwd %quickref %recall %reset %run %save %sc %store %time %timeit %who %whos %%bash %%capture %%html %%javascript %%latex %%python %%time %%timeit %%writefile");
        return result;
    }
    if (name == QLatin1String("psql"))
        return words("\\a \\c \\conninfo \\copy \\d \\da \\db \\dc \\dd \\df \\di \\dl \\dn \\dp \\ds \\dt \\du \\dv \\dx \\e \\echo \\f \\g \\gdesc \\gexec \\h \\i \\ir \\l \\o \\p \\pset \\q \\r \\s \\set \\timing \\unset \\watch \\x");
    if (name == QLatin1String("sqlite3"))
        return words(".backup .bail .cd .changes .clone .databases .dbconfig .dump .echo .eqp .exit .headers .help .import .indexes .limit .load .mode .nullvalue .once .open .output .parameter .print .prompt .quit .read .restore .save .schema .separator .shell .show .stats .tables .timeout .timer .width");
    if (name == QLatin1String("node"))
        return words(".break .clear .editor .exit .help .load .save");
    return {};
}
}

Completion completeProgram(const QString &program, const QString &line, int cursor) {
    Completion result;
    cursor = qBound(0, cursor, line.size());
    int start = cursor;
    while (start > 0 && !line.at(start - 1).isSpace()
           && line.at(start - 1) != QLatin1Char('(')) --start;
    int end = cursor;
    while (end < line.size() && !line.at(end).isSpace()
           && line.at(end) != QLatin1Char('(') && line.at(end) != QLatin1Char(')')) ++end;
    result.start = start;
    result.length = end - start;
    const QString prefix = line.mid(start, cursor - start);
    for (const QString &word : vocabulary(program)) {
        if (!word.startsWith(prefix)) continue;
        result.inserts << word;
        result.labels << word;
    }
    if (!result.inserts.isEmpty()) {
        result.common = result.inserts.first();
        for (const QString &word : result.inserts)
            while (!word.startsWith(result.common)) result.common.chop(1);
    }
    result.commands = true;
    return result;
}

} // namespace relay
