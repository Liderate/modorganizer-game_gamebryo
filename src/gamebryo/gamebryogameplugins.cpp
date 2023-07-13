#include "gamebryogameplugins.h"
#include <imodinterface.h>
#include <iplugingame.h>
#include <ipluginlist.h>
#include <report.h>
#include <safewritefile.h>
#include <scopeguard.h>
#include <utility.h>

#include <QDateTime>
#include <QDir>
#include <QString>
#include <QStringEncoder>
#include <QStringList>

using MOBase::IOrganizer;
using MOBase::IPluginList;
using MOBase::reportError;
using MOBase::SafeWriteFile;

GamebryoGamePlugins::GamebryoGamePlugins(IOrganizer* organizer) : m_Organizer(organizer)
{}

void GamebryoGamePlugins::writePluginLists(const IPluginList* pluginList)
{
  if (!m_LastRead.isValid()) {
    // attempt to write uninitialized plugin lists
    return;
  }

  writePluginList(pluginList, m_Organizer->profile()->absolutePath() + "/plugins.txt");
  writeLoadOrderList(pluginList,
                     m_Organizer->profile()->absolutePath() + "/loadorder.txt");

  m_LastRead = QDateTime::currentDateTime();
}

void GamebryoGamePlugins::readPluginLists(MOBase::IPluginList* pluginList)
{
  QString loadOrderPath = organizer()->profile()->absolutePath() + "/loadorder.txt";
  QString pluginsPath   = organizer()->profile()->absolutePath() + "/plugins.txt";

  bool loadOrderIsNew = !m_LastRead.isValid() || !QFileInfo(loadOrderPath).exists() ||
                        QFileInfo(loadOrderPath).lastModified() > m_LastRead;
  bool pluginsIsNew =
      !m_LastRead.isValid() || QFileInfo(pluginsPath).lastModified() > m_LastRead;

  if (loadOrderIsNew || !pluginsIsNew) {
    // read both files if they are both new or both older than the last read
    QStringList loadOrder = readLoadOrderList(pluginList, loadOrderPath);
    pluginList->setLoadOrder(loadOrder);
    readPluginList(pluginList);
  } else {
    // If the plugins is new but not loadorder, we must reparse the load order from the
    // plugin files
    QStringList loadOrder = readPluginList(pluginList);
    pluginList->setLoadOrder(loadOrder);
  }

  m_LastRead = QDateTime::currentDateTime();
}

QStringList GamebryoGamePlugins::getLoadOrder()
{
  QString loadOrderPath = organizer()->profile()->absolutePath() + "/loadorder.txt";
  QString pluginsPath   = organizer()->profile()->absolutePath() + "/plugins.txt";

  bool loadOrderIsNew = !m_LastRead.isValid() || !QFileInfo(loadOrderPath).exists() ||
                        QFileInfo(loadOrderPath).lastModified() > m_LastRead;
  bool pluginsIsNew =
      !m_LastRead.isValid() || QFileInfo(pluginsPath).lastModified() > m_LastRead;

  if (loadOrderIsNew || !pluginsIsNew) {
    return readLoadOrderList(m_Organizer->pluginList(), loadOrderPath);
  } else {
    return readPluginList(m_Organizer->pluginList());
  }
}

void GamebryoGamePlugins::writePluginList(const MOBase::IPluginList* pluginList,
                                          const QString& filePath)
{
  return writeList(pluginList, filePath, false);
}

void GamebryoGamePlugins::writeLoadOrderList(const MOBase::IPluginList* pluginList,
                                             const QString& filePath)
{
  return writeList(pluginList, filePath, true);
}

void GamebryoGamePlugins::writeList(const IPluginList* pluginList,
                                    const QString& filePath, bool loadOrder)
{
  SafeWriteFile file(filePath);

  QStringEncoder encoder = loadOrder
                               ? QStringEncoder(QStringConverter::Encoding::Utf8)
                               : QStringEncoder(QStringConverter::Encoding::System);

  file->resize(0);

  file->write(
      encoder.encode("# This file was automatically generated by Mod Organizer.\r\n"));

  bool invalidFileNames = false;
  int writtenCount      = 0;

  QStringList plugins = pluginList->pluginNames();
  std::sort(plugins.begin(), plugins.end(),
            [pluginList](const QString& lhs, const QString& rhs) {
              return pluginList->priority(lhs) < pluginList->priority(rhs);
            });

  for (const QString& pluginName : plugins) {
    if (loadOrder || (pluginList->state(pluginName) == IPluginList::STATE_ACTIVE)) {
      auto result = encoder.encode(pluginName);
      if (encoder.hasError()) {
        invalidFileNames = true;
        qCritical("invalid plugin name %s", qUtf8Printable(pluginName));
      } else {
        file->write(result);
      }
      file->write("\r\n");
      ++writtenCount;
    }
  }

  if (invalidFileNames) {
    reportError(QObject::tr("Some of your plugins have invalid names! These "
                            "plugins can not be loaded by the game. Please see "
                            "mo_interface.log for a list of affected plugins "
                            "and rename them."));
  }

  if (writtenCount == 0) {
    qWarning("plugin list would be empty, this is almost certainly wrong. Not "
             "saving.");
  } else {
    file.commitIfDifferent(m_LastSaveHash[filePath]);
  }
}

QStringList GamebryoGamePlugins::readLoadOrderList(MOBase::IPluginList* pluginList,
                                                   const QString& filePath)
{
  QStringList pluginNames = organizer()->managedGame()->primaryPlugins();

  std::set<QString> pluginLookup;
  for (auto&& name : pluginNames) {
    pluginLookup.insert(name.toLower());
  }

  const auto b = MOBase::forEachLineInFile(filePath, [&](QString s) {
    if (!pluginLookup.contains(s.toLower())) {
      pluginLookup.insert(s);
      pluginNames.push_back(std::move(s));
    }
  });

  if (!b) {
    return readPluginList(pluginList);
  }

  return pluginNames;
}

QStringList GamebryoGamePlugins::readPluginList(MOBase::IPluginList* pluginList)
{
  QStringList primary = organizer()->managedGame()->primaryPlugins();
  for (const QString& pluginName : primary) {
    if (pluginList->state(pluginName) != IPluginList::STATE_MISSING) {
      pluginList->setState(pluginName, IPluginList::STATE_ACTIVE);
    }
  }
  QStringList plugins = pluginList->pluginNames();
  QStringList pluginsClone(plugins);
  // Do not sort the primary plugins. Their load order should be locked as defined in
  // "primaryPlugins".
  for (const auto& plugin : pluginsClone) {
    if (primary.contains(plugin, Qt::CaseInsensitive))
      plugins.removeAll(plugin);
  }

  // Always use filetime loadorder to get the actual load order
  std::sort(plugins.begin(), plugins.end(),
            [&](const QString& lhs, const QString& rhs) {
              MOBase::IModInterface* lhm =
                  organizer()->modList()->getMod(pluginList->origin(lhs));
              MOBase::IModInterface* rhm =
                  organizer()->modList()->getMod(pluginList->origin(rhs));
              QDir lhd = organizer()->managedGame()->dataDirectory();
              QDir rhd = organizer()->managedGame()->dataDirectory();
              if (lhm != nullptr)
                lhd = lhm->absolutePath();
              if (rhm != nullptr)
                rhd = rhm->absolutePath();
              QString lhp = lhd.absoluteFilePath(lhs);
              QString rhp = rhd.absoluteFilePath(rhs);
              return QFileInfo(lhp).lastModified() < QFileInfo(rhp).lastModified();
            });

  // Determine plugin active state by the plugins.txt file.
  bool pluginsTxtExists = true;
  QString filePath      = organizer()->profile()->absolutePath() + "/plugins.txt";
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    pluginsTxtExists = false;
  }
  ON_BLOCK_EXIT([&]() {
    file.close();
  });

  if (file.size() == 0) {
    // MO stores at least a header in the file. if it's completely empty the
    // file is broken
    pluginsTxtExists = false;
  }

  QStringList activePlugins;
  QStringList inactivePlugins;
  if (pluginsTxtExists) {
    while (!file.atEnd()) {
      QByteArray line = file.readLine();
      QString pluginName;
      if ((line.size() > 0) && (line.at(0) != '#')) {
        QStringEncoder encoder(QStringConverter::Encoding::System);
        pluginName = encoder.encode(line.trimmed().constData());
      }
      if (pluginName.size() > 0) {
        pluginList->setState(pluginName, IPluginList::STATE_ACTIVE);
        activePlugins.push_back(pluginName);
      }
    }

    for (const auto& pluginName : plugins) {
      if (!activePlugins.contains(pluginName, Qt::CaseInsensitive)) {
        pluginList->setState(pluginName, IPluginList::STATE_INACTIVE);
      }
    }
  } else {
    for (const QString& pluginName : plugins) {
      pluginList->setState(pluginName, IPluginList::STATE_INACTIVE);
    }
  }

  return primary + plugins;
}

bool GamebryoGamePlugins::lightPluginsAreSupported()
{
  return false;
}
