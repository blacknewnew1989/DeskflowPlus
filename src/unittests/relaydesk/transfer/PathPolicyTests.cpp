// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/PathPolicy.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

Q_DECLARE_METATYPE(relaydesk::transfer::PathError)

using namespace relaydesk::transfer;

class PathPolicyTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void acceptsPortableUnicodePath();
  void normalizesUnicodeAndBuildsStableCollisionKey();
  void caseOnlyNamesShareCollisionKey();
  void rejectsEmptyAndControl_data();
  void rejectsEmptyAndControl();
  void rejectsAbsoluteAndDrivePaths_data();
  void rejectsAbsoluteAndDrivePaths();
  void rejectsBackslashProtocolPath();
  void rejectsTraversal_data();
  void rejectsTraversal();
  void rejectsEmptyComponents_data();
  void rejectsEmptyComponents();
  void rejectsInvalidCharacters_data();
  void rejectsInvalidCharacters();
  void rejectsWindowsReservedNames_data();
  void rejectsWindowsReservedNames();
  void allowsNonReservedSimilarName();
  void rejectsTrailingDotOrSpace_data();
  void rejectsTrailingDotOrSpace();
  void enforcesComponentUtf8Limit();
  void enforcesPathUtf8Limit();
  void enforcesDepthLimit();
  void joinsLexicallyUnderAbsoluteRoot();
  void rejectsNonAbsoluteRootAndTraversal();
  void entryPolicyIncludesOnlyRegularFilesAndDirectories();
};

void PathPolicyTests::acceptsPortableUnicodePath()
{
  const auto result = PathPolicy::validateRelative(QStringLiteral("项目/assets/hello 😀.txt"));

  QVERIFY2(result.ok, qPrintable(result.diagnostic));
  QCOMPARE(result.normalized, QStringLiteral("项目/assets/hello 😀.txt"));
  QVERIFY(!result.collisionKey.isEmpty());
}

void PathPolicyTests::normalizesUnicodeAndBuildsStableCollisionKey()
{
  const QString composed = QStringLiteral("Café/Résumé.txt");
  const QString decomposed = QStringLiteral("Cafe\u0301/Re\u0301sume\u0301.txt");
  const auto composedResult = PathPolicy::validateRelative(composed);
  const auto decomposedResult = PathPolicy::validateRelative(decomposed);

  QVERIFY(composedResult.ok);
  QVERIFY(decomposedResult.ok);
  QCOMPARE(decomposedResult.normalized, composed);
  QCOMPARE(decomposedResult.collisionKey, composedResult.collisionKey);
}

void PathPolicyTests::caseOnlyNamesShareCollisionKey()
{
  const auto upper = PathPolicy::validateRelative(QStringLiteral("Folder/Report.TXT"));
  const auto lower = PathPolicy::validateRelative(QStringLiteral("folder/report.txt"));

  QVERIFY(upper.ok);
  QVERIFY(lower.ok);
  QVERIFY(upper.normalized != lower.normalized);
  QCOMPARE(upper.collisionKey, lower.collisionKey);
}

void PathPolicyTests::rejectsEmptyAndControl_data()
{
  QTest::addColumn<QString>("path");
  QTest::newRow("empty") << QString{};
  QTest::newRow("nul") << QStringLiteral("a\0b.txt");
  QString control = QStringLiteral("ab.txt");
  control.insert(1, QChar(0x001f));
  QTest::newRow("control") << control;
  QString unpairedSurrogate(QStringLiteral("bad"));
  unpairedSurrogate.append(QChar(0xd800));
  QTest::newRow("unpaired-surrogate") << unpairedSurrogate;
}

void PathPolicyTests::rejectsEmptyAndControl()
{
  QFETCH(QString, path);
  const auto result = PathPolicy::validateRelative(path);

  QVERIFY(!result.ok);
  QVERIFY(result.error == PathError::Empty || result.error == PathError::InvalidUnicodeOrControl);
}

void PathPolicyTests::rejectsAbsoluteAndDrivePaths_data()
{
  QTest::addColumn<QString>("path");
  QTest::newRow("posix") << QStringLiteral("/tmp/a.txt");
  QTest::newRow("drive-forward") << QStringLiteral("C:/a.txt");
  QTest::newRow("drive-backslash") << QStringLiteral("C:\\a.txt");
  QTest::newRow("drive-relative") << QStringLiteral("C:a.txt");
  QTest::newRow("unc-backslash") << QStringLiteral("\\\\server\\share\\a.txt");
  QTest::newRow("unc-forward") << QStringLiteral("//server/share/a.txt");
}

void PathPolicyTests::rejectsAbsoluteAndDrivePaths()
{
  QFETCH(QString, path);
  const auto result = PathPolicy::validateRelative(path);

  QVERIFY(!result.ok);
  QVERIFY(result.error == PathError::Absolute);
}

void PathPolicyTests::rejectsBackslashProtocolPath()
{
  const auto result = PathPolicy::validateRelative(QStringLiteral("folder\\file.txt"));

  QVERIFY(!result.ok);
  QVERIFY(result.error == PathError::InvalidSeparator);
}

void PathPolicyTests::rejectsTraversal_data()
{
  QTest::addColumn<QString>("path");
  QTest::addColumn<PathError>("error");
  QTest::newRow("parent-prefix") << QStringLiteral("../a.txt") << PathError::ParentTraversal;
  QTest::newRow("parent-middle") << QStringLiteral("a/../../b.txt") << PathError::ParentTraversal;
  QTest::newRow("dot") << QStringLiteral("a/./b.txt") << PathError::DotComponent;
}

void PathPolicyTests::rejectsTraversal()
{
  QFETCH(QString, path);
  QFETCH(PathError, error);
  const auto result = PathPolicy::validateRelative(path);

  QVERIFY(!result.ok);
  QVERIFY(result.error == error);
}

void PathPolicyTests::rejectsEmptyComponents_data()
{
  QTest::addColumn<QString>("path");
  QTest::newRow("middle") << QStringLiteral("a//b.txt");
  QTest::newRow("trailing") << QStringLiteral("a/b/");
}

void PathPolicyTests::rejectsEmptyComponents()
{
  QFETCH(QString, path);
  const auto result = PathPolicy::validateRelative(path);

  QVERIFY(!result.ok);
  QVERIFY(result.error == PathError::EmptyComponent);
}

void PathPolicyTests::rejectsInvalidCharacters_data()
{
  QTest::addColumn<QString>("path");
  for (const QChar character : QStringLiteral("<>:\"|?*")) {
    const QString path = QStringLiteral("file%1name.txt").arg(character);
    QTest::newRow(qPrintable(QStringLiteral("u+%1").arg(character.unicode(), 4, 16, QChar(u'0')))) << path;
  }
}

void PathPolicyTests::rejectsInvalidCharacters()
{
  QFETCH(QString, path);
  const auto result = PathPolicy::validateRelative(path);

  QVERIFY(!result.ok);
  QVERIFY(result.error == PathError::InvalidCharacter);
}

void PathPolicyTests::rejectsWindowsReservedNames_data()
{
  QTest::addColumn<QString>("path");
  QTest::newRow("con") << QStringLiteral("CON");
  QTest::newRow("con-extension") << QStringLiteral("con.txt");
  QTest::newRow("nul-case") << QStringLiteral("NuL.bin");
  QTest::newRow("prn-nested") << QStringLiteral("folder/PRN.log");
  QTest::newRow("aux") << QStringLiteral("AUX");
  QTest::newRow("com1") << QStringLiteral("COM1");
  QTest::newRow("com9-extension") << QStringLiteral("com9.log");
  QTest::newRow("lpt1") << QStringLiteral("LPT1");
  QTest::newRow("lpt9-extension") << QStringLiteral("lpt9.log");
  QTest::newRow("superscript") << QStringLiteral("COM\u00b9.txt");
}

void PathPolicyTests::rejectsWindowsReservedNames()
{
  QFETCH(QString, path);
  const auto result = PathPolicy::validateRelative(path);

  QVERIFY(!result.ok);
  QVERIFY(result.error == PathError::WindowsReservedName);
}

void PathPolicyTests::allowsNonReservedSimilarName()
{
  QVERIFY(PathPolicy::validateRelative(QStringLiteral("COM0.txt")).ok);
  QVERIFY(PathPolicy::validateRelative(QStringLiteral("COM10.txt")).ok);
  QVERIFY(PathPolicy::validateRelative(QStringLiteral("console.txt")).ok);
}

void PathPolicyTests::rejectsTrailingDotOrSpace_data()
{
  QTest::addColumn<QString>("path");
  QTest::newRow("dot") << QStringLiteral("name.");
  QTest::newRow("space") << QStringLiteral("name ");
  QTest::newRow("nested") << QStringLiteral("folder./file.txt");
}

void PathPolicyTests::rejectsTrailingDotOrSpace()
{
  QFETCH(QString, path);
  const auto result = PathPolicy::validateRelative(path);

  QVERIFY(!result.ok);
  QVERIFY(result.error == PathError::WindowsTrailingDotOrSpace);
}

void PathPolicyTests::enforcesComponentUtf8Limit()
{
  PathLimits limits;
  limits.maxComponentUtf8Bytes = 4;
  QVERIFY(PathPolicy::validateRelative(QStringLiteral("éé"), limits).ok);

  const auto result = PathPolicy::validateRelative(QStringLiteral("ééé"), limits);
  QVERIFY(!result.ok);
  QVERIFY(result.error == PathError::ComponentTooLong);
}

void PathPolicyTests::enforcesPathUtf8Limit()
{
  PathLimits limits;
  limits.maxUtf8Bytes = 5;
  const auto result = PathPolicy::validateRelative(QStringLiteral("abc/de"), limits);

  QVERIFY(!result.ok);
  QVERIFY(result.error == PathError::PathTooLong);
}

void PathPolicyTests::enforcesDepthLimit()
{
  PathLimits limits;
  limits.maxDepth = 2;
  const auto result = PathPolicy::validateRelative(QStringLiteral("a/b/c"), limits);

  QVERIFY(!result.ok);
  QVERIFY(result.error == PathError::TooDeep);
}

void PathPolicyTests::joinsLexicallyUnderAbsoluteRoot()
{
  QTemporaryDir temporaryDirectory;
  QVERIFY(temporaryDirectory.isValid());

  QString output;
  const auto result =
      PathPolicy::joinLexicallyUnderRoot(temporaryDirectory.path(), QStringLiteral("项目/Report.txt"), output);

  QVERIFY2(result.ok, qPrintable(result.diagnostic));
  const QString cleanRoot = QDir::fromNativeSeparators(QDir::cleanPath(temporaryDirectory.path()));
  const QString cleanOutput = QDir::fromNativeSeparators(output);
  QVERIFY(cleanOutput.startsWith(cleanRoot + u'/'));
  QVERIFY(cleanOutput.endsWith(QStringLiteral("项目/Report.txt")));
}

void PathPolicyTests::rejectsNonAbsoluteRootAndTraversal()
{
  QString output = QStringLiteral("must be cleared");
  const auto relativeRoot =
      PathPolicy::joinLexicallyUnderRoot(QStringLiteral("relative/root"), QStringLiteral("file.txt"), output);
  QVERIFY(!relativeRoot.ok);
  QVERIFY(relativeRoot.error == PathError::RootNotAbsolute);
  QVERIFY(output.isEmpty());

  QTemporaryDir temporaryDirectory;
  QVERIFY(temporaryDirectory.isValid());
  const auto traversal =
      PathPolicy::joinLexicallyUnderRoot(temporaryDirectory.path(), QStringLiteral("../escape.txt"), output);
  QVERIFY(!traversal.ok);
  QVERIFY(traversal.error == PathError::ParentTraversal);
  QVERIFY(output.isEmpty());
}

void PathPolicyTests::entryPolicyIncludesOnlyRegularFilesAndDirectories()
{
  QCOMPARE(PathPolicy::entryPolicy(ManifestEntryKind::RegularFile).disposition, EntryDisposition::Include);
  QCOMPARE(PathPolicy::entryPolicy(ManifestEntryKind::Directory).disposition, EntryDisposition::Include);

  const auto symbolicLink = PathPolicy::entryPolicy(ManifestEntryKind::SymbolicLink);
  QCOMPARE(symbolicLink.disposition, EntryDisposition::Skip);
  QVERIFY(!symbolicLink.diagnostic.isEmpty());

  const auto special = PathPolicy::entryPolicy(ManifestEntryKind::Special);
  QCOMPARE(special.disposition, EntryDisposition::Skip);
  QVERIFY(!special.diagnostic.isEmpty());
}

QTEST_MAIN(PathPolicyTests)

#include "PathPolicyTests.moc"
