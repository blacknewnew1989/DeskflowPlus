// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/filetransfer/FrameCodec.h"

#include <QTest>
#include <QtEndian>

using namespace relaydesk::filetransfer;

class FrameCodecTests final : public QObject {
    Q_OBJECT

private slots:
    void roundTrip();
    void fragmentedHeaderAndBody();
    void stickyFrames();
    void invalidMagic();
    void unsupportedVersion();
    void metadataLimit();
    void payloadLimit();
};

void FrameCodecTests::roundTrip()
{
    Frame source;
    source.type = MessageType::FileChunk;
    source.flags = AckRequired;
    source.streamId = 42;
    source.metadata = QByteArrayLiteral("meta");
    source.payload = QByteArrayLiteral("ABC");

    QString encodeError;
    QByteArray buffer = FrameCodec::encode(source, {}, &encodeError);
    QVERIFY2(!buffer.isEmpty(), qPrintable(encodeError));

    Frame decoded;
    const DecodeResult result = FrameCodec::tryDecode(buffer, decoded);

    QVERIFY(result.status == DecodeStatus::FrameReady);
    QVERIFY(result.error == DecodeError::None);
    QVERIFY(decoded == source);
    QVERIFY(buffer.isEmpty());
}

void FrameCodecTests::fragmentedHeaderAndBody()
{
    Frame source;
    source.type = MessageType::TransferOffer;
    source.metadata = QByteArrayLiteral("1234567890");

    const QByteArray encoded = FrameCodec::encode(source);
    QVERIFY(!encoded.isEmpty());

    QByteArray buffer;
    Frame decoded;

    for (qsizetype index = 0; index < encoded.size() - 1; ++index) {
        buffer.append(encoded.at(index));
        const DecodeResult result = FrameCodec::tryDecode(buffer, decoded);
        QVERIFY(result.status == DecodeStatus::NeedMoreData);
    }

    buffer.append(encoded.back());
    const DecodeResult result = FrameCodec::tryDecode(buffer, decoded);
    QVERIFY(result.status == DecodeStatus::FrameReady);
    QVERIFY(decoded == source);
    QVERIFY(buffer.isEmpty());
}

void FrameCodecTests::stickyFrames()
{
    Frame first;
    first.type = MessageType::Heartbeat;
    first.streamId = 0;

    Frame second;
    second.type = MessageType::FileChunk;
    second.streamId = 7;
    second.payload = QByteArrayLiteral("data");

    QByteArray buffer = FrameCodec::encode(first) + FrameCodec::encode(second);

    Frame decoded;
    QVERIFY(
        FrameCodec::tryDecode(buffer, decoded).status ==
        DecodeStatus::FrameReady);
    QVERIFY(decoded == first);
    QVERIFY(!buffer.isEmpty());

    QVERIFY(
        FrameCodec::tryDecode(buffer, decoded).status ==
        DecodeStatus::FrameReady);
    QVERIFY(decoded == second);
    QVERIFY(buffer.isEmpty());
}

void FrameCodecTests::invalidMagic()
{
    Frame frame;
    QByteArray buffer = FrameCodec::encode(frame);
    QVERIFY(buffer.size() >= kFixedHeaderBytes);
    buffer[0] = 'X';

    Frame output;
    const DecodeResult result = FrameCodec::tryDecode(buffer, output);
    QVERIFY(result.status == DecodeStatus::ProtocolError);
    QVERIFY(result.error == DecodeError::InvalidMagic);
    QVERIFY(!buffer.isEmpty());
}

void FrameCodecTests::unsupportedVersion()
{
    Frame frame;
    QByteArray buffer = FrameCodec::encode(frame);
    qToBigEndian<quint16>(
        2, reinterpret_cast<uchar*>(buffer.data() + 4));

    Frame output;
    const DecodeResult result = FrameCodec::tryDecode(buffer, output);
    QVERIFY(result.status == DecodeStatus::ProtocolError);
    QVERIFY(result.error == DecodeError::UnsupportedVersion);
}

void FrameCodecTests::metadataLimit()
{
    Frame frame;
    QByteArray buffer = FrameCodec::encode(frame);
    qToBigEndian<quint32>(
        1025, reinterpret_cast<uchar*>(buffer.data() + 12));

    ProtocolLimits limits;
    limits.maxMetadataBytes = 1024;
    limits.maxBufferedFrameBytes = 2048;

    Frame output;
    const DecodeResult result =
        FrameCodec::tryDecode(buffer, output, limits);
    QVERIFY(result.status == DecodeStatus::ProtocolError);
    QVERIFY(result.error == DecodeError::MetadataTooLarge);
}

void FrameCodecTests::payloadLimit()
{
    Frame frame;
    QByteArray buffer = FrameCodec::encode(frame);
    qToBigEndian<quint64>(
        1025, reinterpret_cast<uchar*>(buffer.data() + 16));

    ProtocolLimits limits;
    limits.maxPayloadBytes = 1024;
    limits.maxBufferedFrameBytes = 2048;

    Frame output;
    const DecodeResult result =
        FrameCodec::tryDecode(buffer, output, limits);
    QVERIFY(result.status == DecodeStatus::ProtocolError);
    QVERIFY(result.error == DecodeError::PayloadTooLarge);
}

QTEST_MAIN(FrameCodecTests)
#include "FrameCodecTests.moc"
