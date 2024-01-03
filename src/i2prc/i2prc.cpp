#include <iostream>

#include "api.h"
#include "PeerLink.h"
#include "I2PRCContext.h"
#include "I2PRCService.h"
#include "Config.h"
#include "Transports.h"
#include "Log.h"

#define C2CP_SERVER_PORT 23333

void InitI2P(int argc, char** argv)
{
    i2p::config::Init ();
    i2p::config::ParseCmdline (argc, argv, true); // ignore unknown options and help
    i2p::config::Finalize ();

    std::string datadir; i2p::config::GetOption("datadir", datadir);

    i2p::fs::SetAppName ("i2prc");
    i2p::fs::DetectDataDir(datadir, false);
    i2p::fs::Init();

    bool precomputation; i2p::config::GetOption("precomputation.elgamal", precomputation);
    bool aesni; i2p::config::GetOption("cpuext.aesni", aesni);
    bool forceCpuExt; i2p::config::GetOption("cpuext.force", forceCpuExt);
    i2p::crypto::InitCrypto (precomputation, aesni, forceCpuExt);

    int netID; i2p::config::GetOption("netid", netID);
    i2p::context.SetNetID (netID);

    i2p::context.Init ();

    i2p::transport::InitTransports ();

    bool isFloodfill; i2p::config::GetOption("floodfill", isFloodfill);
    if (isFloodfill)
    {
        LogPrint(eLogInfo, "Daemon: Router configured as floodfill");
        i2p::context.SetFloodfill (true);
    }
    else
        i2p::context.SetFloodfill (false);

    bool transit; i2p::config::GetOption("notransit", transit);
    i2p::context.SetAcceptsTunnels (!transit);
    uint16_t transitTunnels; i2p::config::GetOption("limits.transittunnels", transitTunnels);
    if (isFloodfill && i2p::config::IsDefault ("limits.transittunnels"))
        transitTunnels *= 2; // double default number of transit tunnels for floodfill
    i2p::tunnel::tunnels.SetMaxNumTransitTunnels (transitTunnels);

    /* this section also honors 'floodfill' flag, if set above */
    std::string bandwidth; i2p::config::GetOption("bandwidth", bandwidth);
    if (bandwidth.length () > 0)
    {
        if (bandwidth[0] >= 'K' && bandwidth[0] <= 'X')
        {
            i2p::context.SetBandwidth (bandwidth[0]);
            LogPrint(eLogInfo, "Daemon: Bandwidth set to ", i2p::context.GetBandwidthLimit (), "KBps");
        }
        else
        {
            auto value = std::atoi(bandwidth.c_str());
            if (value > 0)
            {
                i2p::context.SetBandwidth (value);
                LogPrint(eLogInfo, "Daemon: Bandwidth set to ", i2p::context.GetBandwidthLimit (), " KBps");
            }
            else
            {
                LogPrint(eLogInfo, "Daemon: Unexpected bandwidth ", bandwidth, ". Set to 'low'");
                i2p::context.SetBandwidth (i2p::data::CAPS_FLAG_LOW_BANDWIDTH2);
            }
        }
    }
    else if (isFloodfill)
    {
        LogPrint(eLogInfo, "Daemon: Floodfill bandwidth set to 'extra'");
        i2p::context.SetBandwidth (i2p::data::CAPS_FLAG_EXTRA_BANDWIDTH2);
    }
    else
    {
        LogPrint(eLogInfo, "Daemon: bandwidth set to 'low'");
        i2p::context.SetBandwidth (i2p::data::CAPS_FLAG_LOW_BANDWIDTH2);
    }

    int shareRatio; i2p::config::GetOption("share", shareRatio);
    i2p::context.SetShareRatio (shareRatio);

    std::string family; i2p::config::GetOption("family", family);
    i2p::context.SetFamily (family);
    if (family.length () > 0)
        LogPrint(eLogInfo, "Daemon: Router family set to ", family);

    bool trust; i2p::config::GetOption("trust.enabled", trust);
    if (trust)
    {
        LogPrint(eLogInfo, "Daemon: Explicit trust enabled");
        std::string fam; i2p::config::GetOption("trust.family", fam);
        std::string routers; i2p::config::GetOption("trust.routers", routers);
        bool restricted = false;
        if (fam.length() > 0)
        {
            std::set<std::string> fams;
            size_t pos = 0, comma;
            do
            {
                comma = fam.find (',', pos);
                fams.insert (fam.substr (pos, comma != std::string::npos ? comma - pos : std::string::npos));
                pos = comma + 1;
            }
            while (comma != std::string::npos);
            i2p::transport::transports.RestrictRoutesToFamilies(fams);
            restricted = fams.size() > 0;
        }
        if (routers.length() > 0) {
            std::set<i2p::data::IdentHash> idents;
            size_t pos = 0, comma;
            do
            {
                comma = routers.find (',', pos);
                i2p::data::IdentHash ident;
                ident.FromBase64 (routers.substr (pos, comma != std::string::npos ? comma - pos : std::string::npos));
                idents.insert (ident);
                pos = comma + 1;
            }
            while (comma != std::string::npos);
            LogPrint(eLogInfo, "Daemon: Setting restricted routes to use ", idents.size(), " trusted routers");
            i2p::transport::transports.RestrictRoutesToRouters(idents);
            restricted = idents.size() > 0;
        }
        if(!restricted)
            LogPrint(eLogError, "Daemon: No trusted routers of families specified");
    }

    bool hidden; i2p::config::GetOption("trust.hidden", hidden);
    if (hidden)
    {
        LogPrint(eLogInfo, "Daemon: Hidden mode enabled");
        i2p::context.SetHidden(true);
    }
}

void StartI2P()
{
    i2p::log::Logger().SendTo (i2p::fs::DataDirPath (i2p::fs::GetAppName () + ".log"));
    i2p::log::Logger().Start ();
    LogPrint(eLogInfo, "API: Starting NetDB");
    i2p::data::netdb.Start();
    LogPrint(eLogInfo, "API: Starting Transports");
    i2p::transport::transports.Start();
    LogPrint(eLogInfo, "API: Starting Tunnels");
    i2p::tunnel::tunnels.Start();
    LogPrint(eLogInfo, "API: Starting Router context");
    i2p::context.Start();
}



int main(int argc, char **argv, char **envp)
{
    InitI2P(argc, argv);
    auto clientdestination = i2p::api::CreateLocalDestination(true);

    std::string dest_hash =
        clientdestination->GetIdentity()->GetIdentHash().ToBase32();

    std::cout << dest_hash << std::endl;
    auto pl = PreMeshnet(argv[1], dest_hash);
    auto v = pl.GetRouterInfo();
    std::cout << "loading..." << std::endl;

    StartI2P();

    for (auto& i : v) {
        i2p::data::netdb.AddRouterInfo(i.second.bufri, i.second.risize);
        i2p::client::RCCTX.InsertHashMap(i.first, std::string(reinterpret_cast<char*>(i.second.hashbuf), i.second.hashsize));
    }
    std::cout << "done!" << std::endl;

    auto c2pd = std::make_shared<i2p::client::I2PRCService>(clientdestination);

    i2p::client::RCCTX.Init(c2pd, "127.0.0.1", C2CP_SERVER_PORT);
    i2p::client::RCCTX.Start();



    while (1) {
        sleep(1);
    }

    i2p::api::StopI2P();
    return 0;
}
