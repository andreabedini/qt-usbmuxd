// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Andrea Bedini

#include "qusbmuxplist_p.h"

#include <QtCore/qdatetime.h>
#include <QtCore/qxmlstream.h>

namespace QUsbmuxPlist {

namespace {

// Indents the way libplist and Apple's encoder do (the root <dict> is not
// indented inside <plist>), so captured traffic diffs cleanly against
// theirs. QXmlStreamWriter's own auto-formatting indents one level deeper.
void indent(QXmlStreamWriter &w, int depth)
{
    w.writeCharacters(QStringLiteral("\n") + QString(depth, u'\t'));
}

void writeValue(QXmlStreamWriter &w, const QVariant &value, int depth)
{
    indent(w, depth);
    switch (value.typeId()) {
    case QMetaType::QVariantMap: {
        const QVariantMap map = value.toMap();
        if (map.isEmpty()) {
            w.writeEmptyElement(QStringLiteral("dict"));
            break;
        }
        w.writeStartElement(QStringLiteral("dict"));
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            indent(w, depth + 1);
            w.writeTextElement(QStringLiteral("key"), it.key());
            writeValue(w, it.value(), depth + 1);
        }
        indent(w, depth);
        w.writeEndElement();
        break;
    }
    case QMetaType::QVariantList:
    case QMetaType::QStringList: {
        const QVariantList list = value.toList();
        if (list.isEmpty()) {
            w.writeEmptyElement(QStringLiteral("array"));
            break;
        }
        w.writeStartElement(QStringLiteral("array"));
        for (const QVariant &item : list)
            writeValue(w, item, depth + 1);
        indent(w, depth);
        w.writeEndElement();
        break;
    }
    case QMetaType::Bool:
        w.writeEmptyElement(value.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
        break;
    case QMetaType::Int:
    case QMetaType::Long:
    case QMetaType::LongLong:
    case QMetaType::Short:
    case QMetaType::Char:
    case QMetaType::SChar:
        w.writeTextElement(QStringLiteral("integer"), QString::number(value.toLongLong()));
        break;
    case QMetaType::UInt:
    case QMetaType::ULong:
    case QMetaType::ULongLong:
    case QMetaType::UShort:
    case QMetaType::UChar:
        w.writeTextElement(QStringLiteral("integer"), QString::number(value.toULongLong()));
        break;
    case QMetaType::Double:
    case QMetaType::Float:
        w.writeTextElement(QStringLiteral("real"), QString::number(value.toDouble(), 'g', 17));
        break;
    case QMetaType::QByteArray:
        w.writeTextElement(QStringLiteral("data"), QString::fromLatin1(value.toByteArray().toBase64()));
        break;
    case QMetaType::QDateTime:
        w.writeTextElement(QStringLiteral("date"),
                           value.toDateTime().toUTC().toString(Qt::ISODate));
        break;
    default:
        w.writeTextElement(QStringLiteral("string"), value.toString());
        break;
    }
}

// Reads the value whose start element the reader is positioned on, and
// leaves the reader on its end element.
QVariant readValue(QXmlStreamReader &r)
{
    const QStringView name = r.name();

    if (name == u"dict") {
        QVariantMap map;
        while (r.readNextStartElement()) {
            if (r.name() != u"key") {
                r.raiseError(QStringLiteral("expected <key> in <dict>, got <%1>").arg(r.name()));
                return {};
            }
            const QString key = r.readElementText();
            if (!r.readNextStartElement()) {
                if (!r.hasError())
                    r.raiseError(QStringLiteral("missing value for key \"%1\"").arg(key));
                return {};
            }
            map.insert(key, readValue(r));
            if (r.hasError())
                return {};
        }
        return map;
    }

    if (name == u"array") {
        QVariantList list;
        while (r.readNextStartElement()) {
            list.append(readValue(r));
            if (r.hasError())
                return {};
        }
        return list;
    }

    if (name == u"true" || name == u"false") {
        const bool value = name == u"true";
        r.skipCurrentElement();
        return value;
    }

    if (name == u"string")
        return r.readElementText();

    if (name == u"integer") {
        const QString text = r.readElementText().trimmed();
        bool ok = false;
        if (const qint64 v = text.toLongLong(&ok); ok)
            return v;
        if (const quint64 v = text.toULongLong(&ok); ok)
            return v;
        r.raiseError(QStringLiteral("bad integer \"%1\"").arg(text));
        return {};
    }

    if (name == u"real") {
        const QString text = r.readElementText().trimmed();
        bool ok = false;
        const double v = text.toDouble(&ok);
        if (!ok) {
            r.raiseError(QStringLiteral("bad real \"%1\"").arg(text));
            return {};
        }
        return v;
    }

    if (name == u"data") {
        // Apple's encoder wraps base64 across lines with tabs; the decoder
        // has to skip whitespace.
        QByteArray encoded = r.readElementText().toLatin1();
        encoded.removeIf([](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; });
        const auto decoded = QByteArray::fromBase64Encoding(
            encoded, QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded) {
            r.raiseError(QStringLiteral("bad base64 in <data>"));
            return {};
        }
        return *decoded;
    }

    if (name == u"date") {
        const QString text = r.readElementText().trimmed();
        QDateTime dt = QDateTime::fromString(text, Qt::ISODate);
        if (!dt.isValid()) {
            r.raiseError(QStringLiteral("bad date \"%1\"").arg(text));
            return {};
        }
        return dt.toUTC();
    }

    r.raiseError(QStringLiteral("unknown plist element <%1>").arg(name));
    return {};
}

} // namespace

QByteArray toXml(const QVariantMap &dict)
{
    QByteArray xml;
    QXmlStreamWriter w(&xml);
    w.writeStartDocument();
    w.writeCharacters(QStringLiteral("\n"));
    w.writeDTD(QStringLiteral("<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                              "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">"));
    w.writeCharacters(QStringLiteral("\n"));
    w.writeStartElement(QStringLiteral("plist"));
    w.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
    writeValue(w, dict, 0);
    w.writeCharacters(QStringLiteral("\n"));
    w.writeEndElement();
    w.writeEndDocument();
    return xml;
}

std::optional<QVariantMap> fromXml(const QByteArray &xml, QString *error)
{
    const auto fail = [&](const QString &message) -> std::optional<QVariantMap> {
        if (error)
            *error = message;
        return std::nullopt;
    };

    if (xml.startsWith("bplist"))
        return fail(QStringLiteral("binary property lists are not supported"));

    QXmlStreamReader r(xml);
    if (!r.readNextStartElement() || r.name() != u"plist")
        return fail(r.hasError() ? r.errorString() : QStringLiteral("not a property list"));
    if (!r.readNextStartElement())
        return fail(r.hasError() ? r.errorString() : QStringLiteral("empty property list"));
    if (r.name() != u"dict")
        return fail(QStringLiteral("top-level value is <%1>, expected <dict>").arg(r.name()));

    const QVariant value = readValue(r);
    if (r.hasError())
        return fail(r.errorString());
    return value.toMap();
}

} // namespace QUsbmuxPlist
