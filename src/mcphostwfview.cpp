#include "mcphostwfview.h"

#include "MainController.h"
#include "SettingsController.h"
#include "audiorouting.h"
#include "mcptools.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QStandardPaths>

namespace
{

QString connStatusName(MainController::connectionStatus_t status)
{
    switch (status)
    {
    case MainController::connDisconnected:
        return QStringLiteral("disconnected");
    case MainController::connConnecting:
        return QStringLiteral("connecting");
    case MainController::connConnected:
        return QStringLiteral("connected");
    }
    return QString::number(int(status));
}

QString pttTypeName(pttType_t ptt)
{
    switch (ptt)
    {
    case pttCIV:
        return QStringLiteral("CI-V");
    case pttRTS:
        return QStringLiteral("RTS");
    case pttDTR:
        return QStringLiteral("DTR");
    }
    return QString::number(int(ptt));
}

QString lanTypeName(connectionType_t type)
{
    switch (type)
    {
    case connectionUSB:
        return QStringLiteral("USB");
    case connectionLAN:
        return QStringLiteral("LAN");
    case connectionWiFi:
        return QStringLiteral("WiFi");
    case connectionWAN:
        return QStringLiteral("WAN");
    }
    return QString::number(int(type));
}

QString txInputName(TxAudioInput input)
{
    switch (input)
    {
    case TxAudioInput::None:
        return QStringLiteral("none");
    case TxAudioInput::LocalInput:
        return QStringLiteral("local");
    case TxAudioInput::WfShare:
        return QStringLiteral("wfshare");
    case TxAudioInput::Tci:
        return QStringLiteral("tci");
    }
    return QString::number(int(input));
}

// Matches MainController::setManufacturer().
QString systemRigLocation()
{
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return QCoreApplication::applicationDirPath() + "/../Resources/rigs";
#elif !defined(Q_OS_LINUX)
    return QCoreApplication::applicationDirPath() + "/rigs";
#else
    return qEnvironmentVariable("APPDIR").isEmpty()
               ? QString(PREFIX "/share/wfview/rigs")
               : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/wfview/rigs";
#endif
}

QJsonObject dirJson(const QString &path)
{
    const QDir dir(path);
    return QJsonObject{{"path", dir.absolutePath()},
                       {"exists", dir.exists()},
                       {"rigFiles", int(dir.entryList({"*.rig", "*.RIG"}, QDir::Files).size())}};
}

} // namespace

McpHostWfview::McpHostWfview(MainController *controller, QObject *parent)
    : McpHost(parent)
    , m_mc(controller)
{
    connect(controller, &MainController::transmittingChanged, this, [this]() {
        if (m_mc && !m_mc->transmitting())
            transmitEnded();
    });
}

QString McpHostWfview::appName() const
{
    return QStringLiteral("wfview");
}

QString McpHostWfview::settingsFileName() const
{
    return m_mc && m_mc->m_settings ? m_mc->m_settings->settingsFileName() : QString();
}

QSettings::Format McpHostWfview::settingsFormat() const
{
    return m_mc && m_mc->m_settings ? m_mc->m_settings->settingsFormat() : QSettings::IniFormat;
}

QJsonObject McpHostWfview::connectionInfo()
{
    if (!m_mc || !m_mc->prefs || !m_mc->udpPrefs || m_mc->shuttingDown)
        return QJsonObject{{"error", "wfview is shutting down"}};

    const preferences *p = m_mc->prefs;
    const udpPreferences *u = m_mc->udpPrefs;

    QString transport = QStringLiteral("serial");
    if (p->wfShareEnabled)
        transport = p->wfShareDirectMode ? QStringLiteral("wfshare-direct") : QStringLiteral("wfshare");
    else if (p->enableLAN)
        transport = QStringLiteral("network");

    QJsonObject d;
    d["status"] = connStatusName(m_mc->connStatus);
    d["transport"] = transport;
    d["manufacturer"] = Mcp::manufacturerName(p->manufacturer);
    d["rigModelName"] = m_mc->m_rigModelName;
    d["radioStatusText"] = m_mc->m_radioStatusText;
    d["footerMessage"] = m_mc->m_footerMessageText;

    d["serial"] = QJsonObject{{"port", p->serialPortRadio},
                              {"baudRate", double(p->serialPortBaud)},
                              {"radioId", Mcp::radioIdJson(p->manufacturer, p->radioCIVAddr)},
                              {"civIsRadioModel", p->CIVisRadioModel},
                              {"pttType", pttTypeName(p->pttType)},
                              {"pollingMs", p->polling_ms},
                              {"virtualSerialPort", p->virtualSerialPort},
                              {"virtualSerialPortUseQueue", p->virtualSerialPortUseQueue}};

    d["network"] = QJsonObject{{"enabled", p->enableLAN},
                               {"connectionType", lanTypeName(u->connectionType)},
                               {"ipAddress", u->ipAddress},
                               {"controlPort", int(u->controlLANPort)},
                               {"serialPort", int(u->serialLANPort)},
                               {"audioPort", int(u->audioLANPort)},
                               {"scopePort", int(u->scopeLANPort)},
                               {"username", u->username},
                               {"password", Mcp::maskSecret(u->password)},
                               {"clientName", u->clientName},
                               {"halfDuplex", u->halfDuplex},
                               {"adminLogin", u->adminLogin},
                               {"waterfallFormat", int(p->waterfallFormat)}};

    d["wfshare"] = QJsonObject{{"enabled", p->wfShareEnabled},
                               {"directMode", p->wfShareDirectMode},
                               {"host", p->wfShareHost},
                               {"port", int(p->wfSharePort)},
                               {"username", p->wfShareUsername},
                               {"password", Mcp::maskSecret(p->wfSharePassword)},
                               {"calledNumber", p->wfShareCalledNumber}};

    const rigCtlD *rigCtl = m_mc->rigCtl;
    d["rigctld"] = QJsonObject{{"enabled", p->enableRigCtlD},
                               {"port", int(p->rigCtlPort)},
                               {"listening", rigCtl && rigCtl->isListening()}};
    d["tci"] = QJsonObject{{"port", int(p->tciPort)}, {"running", m_mc->tci != nullptr}};
    d["virtualSerialTcpPort"] = int(p->tcpPort);

    d["cluster"] = QJsonObject{{"udpEnabled", p->clusterUdpEnable},
                               {"udpPort", p->clusterUdpPort},
                               {"tcpEnabled", p->clusterTcpEnable},
                               {"tcpServer", p->clusterTcpServerName},
                               {"tcpPort", p->clusterTcpPort},
                               {"tcpUser", p->clusterTcpUserName},
                               {"tcpPassword", Mcp::maskSecret(p->clusterTcpPassword)}};

    if (const SERVERCONFIG *server = m_mc->m_settings->getServerConfig())
    {
        QJsonArray users;
        for (const SERVERUSER &user : server->users)
            users.append(QJsonObject{{"username", user.username},
                                     {"password", Mcp::maskSecret(user.password)},
                                     {"userType", int(user.userType)}});
        d["server"] = QJsonObject{{"enabled", server->enabled},
                                  {"listenAddress", server->listenAddress},
                                  {"controlPort", int(server->controlPort)},
                                  {"civPort", int(server->civPort)},
                                  {"audioPort", int(server->audioPort)},
                                  {"scopePort", int(server->scopePort)},
                                  {"users", users}};
    }
    return d;
}

QJsonObject McpHostWfview::audioConfig()
{
    if (!m_mc || !m_mc->prefs || m_mc->shuttingDown)
        return QJsonObject{{"error", "wfview is shutting down"}};

    const preferences *p = m_mc->prefs;
    const txAudioProcessingPrefs &tx = p->txAudioProc;
    const rxAudioProcessingPrefs &rx = p->rxAudioProc;

    QJsonObject d;
    d["audioSystem"] = Mcp::audioTypeName(int(p->audioSystem));
    d["route"] = audioRouteDebugString(m_mc->currentAudioRoute());
    d["activeTxAudioSource"] = txInputName(m_mc->m_activeTxAudioSource);
    d["localRx"] = Mcp::audioSetupToJson(p->rxSetup);
    d["localTx"] = Mcp::audioSetupToJson(p->txSetup);
    d["usbAudioEnabled"] = p->enableUsbAudio;
    d["usbRadioRx"] = Mcp::audioSetupToJson(p->usbRxSetup);
    d["usbRadioTx"] = Mcp::audioSetupToJson(p->usbTxSetup);
    d["openHandlers"] = QJsonObject{{"usbRadioRx", m_mc->usbRadioRxAudio != nullptr},
                                    {"usbRxOutput", m_mc->usbRxOutputAudio != nullptr},
                                    {"tciRxOutput", m_mc->tciRxOutputAudio != nullptr},
                                    {"usbTxInput", m_mc->usbTxInputAudio != nullptr},
                                    {"usbRadioTx", m_mc->usbRadioTxAudio != nullptr},
                                    {"tciTxInput", m_mc->tciTxInputAudio != nullptr}};
    d["gains"] = QJsonObject{{"localAfGain", int(p->localAFgain)},
                             {"rxMuted", m_mc->m_rxMuted},
                             {"monitorEnabled", m_mc->m_monitorEnabled},
                             {"monitorGain", m_mc->m_monitorGain},
                             {"modGainLabel", m_mc->m_modGainLabel},
                             {"modGain", m_mc->m_micGain},
                             {"txPower", m_mc->m_txPower}};
    d["displayedLevels"] = QJsonObject{{"rxAudioLevel", m_mc->m_rxAudioLevel},
                                       {"txAudioLevel", m_mc->m_txAudioLevel},
                                       {"txProcessorInput", m_mc->m_txAudioInputLevel},
                                       {"txProcessorOutput", m_mc->m_txAudioOutputLevel},
                                       {"txGainReduction", m_mc->m_txAudioGainReduction},
                                       {"rxProcessorInput", m_mc->m_rxAudioInputLevel},
                                       {"rxProcessorOutput", m_mc->m_rxAudioOutputLevel}};
    d["txProcessing"] = QJsonObject{{"bypass", tx.bypass},
                                    {"compressor", tx.compEnabled},
                                    {"equalizer", tx.eqEnabled},
                                    {"noiseGate", tx.gateEnabled},
                                    {"inputGainDB", double(tx.inputGainDB)},
                                    {"outputGainDB", double(tx.outputGainDB)}};
    d["rxProcessing"] = QJsonObject{{"bypass", rx.bypass},
                                    {"noiseReduction", rx.nrEnabled},
                                    {"noiseReductionMode", int(rx.nrMode)},
                                    {"equalizer", rx.eqEnabled},
                                    {"outputGainDB", double(rx.outputGainDB)}};
    return d;
}

QJsonObject McpHostWfview::rigFiles()
{
    if (!m_mc || !m_mc->prefs || m_mc->shuttingDown)
        return QJsonObject{{"error", "wfview is shutting down"}};

    QJsonArray rigs;
    const int manufacturer = m_mc->prefs->manufacturer;
    for (auto it = m_mc->rigList.cbegin(); it != m_mc->rigList.cend(); ++it)
    {
        const rigInfo &info = it.value();
        rigs.append(QJsonObject{{"radioId", Mcp::radioIdJson(manufacturer, info.civ)},
                                {"model", info.model},
                                {"version", double(info.version)},
                                {"path", info.path}});
    }

    QJsonObject d;
    d["manufacturer"] = Mcp::manufacturerName(m_mc->prefs->manufacturer);
    d["systemRigFolder"] = dirJson(systemRigLocation());
    d["userRigFolder"] = dirJson(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/rigs");
    d["note"] = QStringLiteral("A user rig file overrides a system one with the same CI-V address when its version "
                               "is the same or newer.");
    d["rigs"] = rigs;
    d["currentRigFile"] = m_mc->rigCaps ? m_mc->rigCaps->filename : QString();
    return d;
}

QJsonObject McpHostWfview::appState()
{
    if (!m_mc || m_mc->shuttingDown)
        return QJsonObject{{"error", "wfview is shutting down"}};

    QJsonArray receivers;
    for (int i = 0; i < m_mc->receivers.size(); ++i)
    {
        ReceiverController *rx = m_mc->receivers.at(i);
        if (!rx)
            continue;
        receivers.append(QJsonObject{{"index", i},
                                     {"title", rx->getTitle()},
                                     {"active", rx->getActive()},
                                     {"frequencyAHz", double(rx->getFrequencyA())},
                                     {"frequencyBHz", double(rx->getFrequencyB())},
                                     {"mode", int(rx->getMode())},
                                     {"dataMode", int(rx->getDataMode())},
                                     {"filter", int(rx->getFilter())}});
    }

    return QJsonObject{{"connection", connStatusName(m_mc->connStatus)},
                       {"rigModelName", m_mc->m_rigModelName},
                       {"radioStatusText", m_mc->m_radioStatusText},
                       {"footerMessage", m_mc->m_footerMessageText},
                       {"transmitting", m_mc->m_transmitting},
                       {"currentReceiver", int(m_mc->currentReceiver)},
                       {"frequencyLocked", m_mc->freqLock},
                       {"split", m_mc->m_splitEnabled},
                       {"dualWatch", m_mc->m_dualWatch},
                       {"ritEnabled", m_mc->m_ritEnabled},
                       {"ritHz", m_mc->m_ritFrequency},
                       {"tuner", m_mc->m_tunerEnabled},
                       {"stepHz", double(m_mc->stepSize)},
                       {"receivers", receivers}};
}

bool McpHostWfview::rigCaps(int radio, rigCapabilities *out, QString *error)
{
    Q_UNUSED(radio)
    if (!m_mc || m_mc->shuttingDown)
    {
        *error = QStringLiteral("wfview is shutting down.");
        return false;
    }
    if (!m_mc->rigCaps)
    {
        *error = QStringLiteral("No radio is connected, so there are no rig capabilities yet.");
        return false;
    }
    *out = *m_mc->rigCaps;
    return true;
}

McpHostResult McpHostWfview::settingOptions()
{
    McpHostResult r;
    SettingsController *settings = m_mc && !m_mc->shuttingDown ? m_mc->getSettings() : nullptr;
    if (!settings)
    {
        r.error = QStringLiteral("wfview is shutting down.");
        return r;
    }

    QStringList keys = settings->optionKeys();
    keys.sort();
    QJsonObject values;
    QJsonArray readOnly;
    for (const QString &key : keys)
    {
        const QVariant value = settings->option(key);
        values[key] = Mcp::isSecretKey(key) ? QJsonValue(Mcp::maskSecret(value.toString())) : Mcp::variantToJson(value);
        if (!Mcp::isWritableOption(key, nullptr))
            readOnly.append(key);
    }

    r.ok = true;
    r.data["options"] = values;
    r.data["notWritable"] = readOnly;
    r.data["unsaved"] = settings->dirty();
    return r;
}

McpHostResult McpHostWfview::setOption(const QString &key, const QJsonValue &value, bool save)
{
    McpHostResult r;
    SettingsController *settings = m_mc && !m_mc->shuttingDown ? m_mc->getSettings() : nullptr;
    if (!settings)
    {
        r.error = QStringLiteral("wfview is shutting down.");
        return r;
    }
    if (!settings->optionKeys().contains(key))
    {
        r.error = QStringLiteral("There is no setting named %1; get_setting_options lists them.").arg(key);
        return r;
    }
    if (!Mcp::isWritableOption(key, &r.error))
        return r;

    // The same path the settings dialog uses, so the change takes effect at once.
    const QVariant before = settings->option(key);
    settings->setOption(key, value.toVariant());
    const QVariant after = settings->option(key);
    if (save)
        settings->save();

    r.ok = true;
    r.data["key"] = key;
    r.data["before"] = Mcp::variantToJson(before);
    r.data["after"] = Mcp::variantToJson(after);
    r.data["changed"] = before != after;
    r.data["saved"] = save;
    r.data["unsaved"] = settings->dirty();
    return r;
}

McpHostResult McpHostWfview::setTransmit(bool on)
{
    McpHostResult r;
    if (!m_mc || m_mc->shuttingDown)
    {
        r.error = QStringLiteral("wfview is shutting down.");
        return r;
    }
    if (!m_mc->rigCaps || !m_mc->rigCaps->hasTransmit || !m_mc->rigCaps->commands.contains(funcTransceiverStatus))
    {
        r.error = QStringLiteral("The connected radio cannot be keyed by wfview.");
        return r;
    }
    if (on && (!m_mc->prefs || !m_mc->prefs->enablePTT))
    {
        r.error = QStringLiteral("PTT is turned off in wfview's settings (Controls.EnablePTT).");
        return r;
    }

    // Goes through wfview's own PTT handling, which also selects the TX audio source.
    m_mc->setTransmit(on);
    r.ok = true;
    r.data["requested"] = on ? QStringLiteral("transmit") : QStringLiteral("receive");
    r.data["transmittingBefore"] = m_mc->m_transmitting;
    return r;
}
