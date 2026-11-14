#pragma once

#include <QDataStream>
#include <QFile>

// Three seconds of mono PCM silence exercises the real decoder without audible test tones.
inline bool writeAudioFixture(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray samples(48000 * 2 * 3, '\0');
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + samples.size());
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(48000)
           << quint32(96000) << quint16(2) << quint16(16);
    stream.writeRawData("data", 4);
    stream << quint32(samples.size());
    stream.writeRawData(samples.constData(), samples.size());
    return stream.status() == QDataStream::Ok && file.flush();
}
