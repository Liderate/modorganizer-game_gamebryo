#ifndef GAMEBRYOBSAINVALIDATION_H
#define GAMEBRYOBSAINVALIDATION_H


#include <QString>
#include <bsainvalidation.h>
#include <dataarchives.h>
#include <memory>

namespace MOBase {
  class IPluginGame;
}

class GamebryoBSAInvalidation : public BSAInvalidation
{
public:

  GamebryoBSAInvalidation(const std::shared_ptr<DataArchives> &dataArchives,
                          const QString &iniFilename,
                          MOBase::IPluginGame *game);

  virtual bool isInvalidationBSA(const QString &bsaName) override;
  virtual void deactivate(MOBase::IProfile *profile) override;
  virtual void activate(MOBase::IProfile *profile) override;

private:

  virtual QString invalidationBSAName() const = 0;
  virtual unsigned long bsaVersion() const = 0; // 0x67 for oblivion, 0x68 for everything else

private:

  std::shared_ptr<DataArchives> m_DataArchives;
  QString m_IniFileName;
  MOBase::IPluginGame *m_Game;

};

#endif // GAMEBRYOBSAINVALIDATION_H
