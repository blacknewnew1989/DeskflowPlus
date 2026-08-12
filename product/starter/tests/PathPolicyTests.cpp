// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/filetransfer/PathPolicy.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

using namespace relaydesk::filetransfer;

class PathPolicyTests final : public QObject {
    Q_OBJECT

private slots:
    void acceptsPortableRelativePath();
    void normalizesUnicode();
    void rejectsTraversal();
    void rejectsAbsolutePaths();
    void rejectsBackslash();
    void rejectsWindowsReserved_data();
    void rejectsWindowsReserved();
    void rejectsWindowsAds();
    void rejectsTrailingDotOrSpace();
    void rejectsEmptyComponents();
    void joinsUnderRoot();
};

void PathPolicyTests::acceptsPortableRelativePath()
{
    const auto result = PathPolicy::validateRelative(
        QStringLiteral("项目/assets/hello 😀.txt"),
        TargetPlatform::Portable);
    QVERIFY2(result.ok, qPrintable(result.diagnostic));
}

void PathPolicyTests::normalizesUnicode()
{
    const QString decomposed =
        QStringLiteral("cafe\u0301/file.txt");
    const auto result = PathPolicy::validateRelative(
        decomposed, TargetPlatform::MacOS);
    QVERIFY(result.ok);
    QCOMPARE(
        result.normalized,
        decomposed.normalized(QString::NormalizationForm_C));
}

void PathPolicyTests::rejectsTraversal()
{
    const auto result = PathPolicy::validateRelative(
        QStringLiteral("a/../b.txt"),
        TargetPlatform::Portable);
    QVERIFY(!result.ok);
    QVERIFY(result.error == PathError::ParentTraversal);
}

void PathPolicyTests::rejectsAbsolutePaths()
{
    const auto posix = PathPolicy::validateRelative(
        QStringLiteral("/tmp/a"), TargetPlatform::MacOS);
    QVERIFY(!posix.ok);
    QVERIFY(posix.error == PathError::Absolute);

    const auto windows = PathPolicy::validateRelative(
        QStringLiteral("C:/a"), TargetPlatform::Windows);
    QVERIFY(!windows.ok);
    QVERIFY(windows.error == PathError::Absolute);
}

void PathPolicyTests::rejectsBackslash()
{
    const auto result = PathPolicy::validateRelative(
        QStringLiteral("a\\b.txt"), TargetPlatform::Windows);
    QVERIFY(!result.ok);
    QVERIFY(result.error == PathError::InvalidSeparator);
}

void PathPolicyTests::rejectsWindowsReserved_data()
{
    QTest::addColumn<QString>("name");
    QTest::newRow("con") << QStringLiteral("CON");
    QTest::newRow("con-extension") << QStringLiteral("con.txt");
    QTest::newRow("nul") << QStringLiteral("NuL.bin");
    QTest::newRow("com1") << QStringLiteral("COM1");
    QTest::newRow("lpt9") << QStringLiteral("lpt9.log");
}

void PathPolicyTests::rejectsWindowsReserved()
{
    QFETCH(QString, name);
    const auto result =
        PathPolicy::validateRelative(name, TargetPlatform::Windows);
    QVERIFY(!result.ok);
    QVERIFY(result.error == PathError::WindowsReservedName);
}

void PathPolicyTests::rejectsWindowsAds()
{
    const auto result = PathPolicy::validateRelative(
        QStringLiteral("file.txt:stream"),
        TargetPlatform::Windows);
    QVERIFY(!result.ok);
    QVERIFY(result.error == PathError::InvalidCharacter);
}

void PathPolicyTests::rejectsTrailingDotOrSpace()
{
    QVERIFY(
        PathPolicy::validateRelative(
            QStringLiteral("name."), TargetPlatform::Windows).error ==
        PathError::WindowsTrailingDotOrSpace);
    QVERIFY(
        PathPolicy::validateRelative(
            QStringLiteral("name "), TargetPlatform::Windows).error ==
        PathError::WindowsTrailingDotOrSpace);
}

void PathPolicyTests::rejectsEmptyComponents()
{
    const auto result = PathPolicy::validateRelative(
        QStringLiteral("a//b"), TargetPlatform::Portable);
    QVERIFY(!result.ok);
    QVERIFY(result.error == PathError::EmptyComponent);
}

void PathPolicyTests::joinsUnderRoot()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString output;
    const auto result = PathPolicy::joinLexicallyUnderRoot(
        temp.path(),
        QStringLiteral("a/b.txt"),
        TargetPlatform::Portable,
        output);
    QVERIFY2(result.ok, qPrintable(result.diagnostic));
    QVERIFY(output.startsWith(QDir::cleanPath(temp.path())));
    QVERIFY(QDir::fromNativeSeparators(output).endsWith(
        QStringLiteral("a/b.txt")));
}

QTEST_MAIN(PathPolicyTests)
#include "PathPolicyTests.moc"
