// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#include "MinerManager.h"

#include <System/EventLock.h>
#include <System/InterruptedException.h>
#include <System/Timer.h>
#include <thread>
#include <chrono>

#include "Common/StringTools.h"
#include <config/CryptoNoteConfig.h>
#include "CryptoNoteCore/CachedBlock.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "Rpc/HttpClient.h"
#include "Rpc/CoreRpcServerCommandsDefinitions.h"
#include "Rpc/JsonRpc.h"

#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/error/en.h"

#include <Utilities/FormatTools.h>

#include <Utilities/ColouredMsg.h>

using namespace CryptoNote;

namespace Miner {

namespace {

MinerEvent BlockMinedEvent()
{
    MinerEvent event;
    event.type = MinerEventType::BLOCK_MINED;
    return event;
}

MinerEvent BlockchainUpdatedEvent()
{
    MinerEvent event;
    event.type = MinerEventType::BLOCKCHAIN_UPDATED;
    return event;
}

void adjustMergeMiningTag(BlockTemplate& blockTemplate)
{
    CachedBlock cachedBlock(blockTemplate);

    if (blockTemplate.majorVersion >= BLOCK_MAJOR_VERSION_2)
    {
        CryptoNote::TransactionExtraMergeMiningTag mmTag;
        mmTag.depth = 0;
        mmTag.merkleRoot = cachedBlock.getAuxiliaryBlockHeaderHash();

        blockTemplate.parentBlock.baseTransaction.extra.clear();
        if (!CryptoNote::appendMergeMiningTagToExtra(blockTemplate.parentBlock.baseTransaction.extra, mmTag))
        {
            throw std::runtime_error("Couldn't append merge mining tag");
        }
    }
}

} // namespace

MinerManager::MinerManager(
    System::Dispatcher& dispatcher,
    const CryptoNote::MiningConfig& config,
    const std::shared_ptr<httplib::Client> httpClient) :

    m_contextGroup(dispatcher),
    m_config(config),
    m_miner(dispatcher),
    m_blockchainMonitor(dispatcher, m_config.scanPeriod, httpClient),
    m_eventOccurred(dispatcher),
    m_lastBlockTimestamp(0),
    m_httpClient(httpClient)
{
}

void MinerManager::start()
{
    BlockMiningParameters params = requestMiningParameters();
    adjustBlockTemplate(params.blockTemplate);

    isRunning = true;

    startBlockchainMonitoring();
    std::thread reporter(std::bind(&MinerManager::printHashRate, this));
    startMining(params);

    eventLoop();
    isRunning = false;
}

void MinerManager::printHashRate()
{
    uint64_t last_hash_count = m_miner.getHashCount();

    while (isRunning)
    {
        std::this_thread::sleep_for(std::chrono::seconds(60));

        uint64_t current_hash_count = m_miner.getHashCount();

        double hashes = static_cast<double>((current_hash_count - last_hash_count) / 60);

        last_hash_count = current_hash_count;

        std::cout << SuccessMsg("\nMining at ")
                  << SuccessMsg(Utilities::get_mining_speed(hashes))
                  << "\n\n";
    }
}

void MinerManager::eventLoop()
{
    size_t blocksMined = 0;

    while(true)
    {
        MinerEvent event = waitEvent();

        switch (event.type)
        {
            case MinerEventType::BLOCK_MINED:
            {
                stopBlockchainMonitoring();

                if (submitBlock(m_minedBlock))
                {
                    m_lastBlockTimestamp = m_minedBlock.timestamp;

                    if (m_config.blocksLimit != 0 && ++blocksMined == m_config.blocksLimit)
                    {
                        std::cout << InformationMsg("Mined requested amount of blocks (")
                                  << InformationMsg(m_config.blocksLimit)
                                  << InformationMsg("). Quitting.\n");
                        return;
                    }
                }

                BlockMiningParameters params = requestMiningParameters();
                adjustBlockTemplate(params.blockTemplate);

                startBlockchainMonitoring();
                startMining(params);
                break;
            }
            case MinerEventType::BLOCKCHAIN_UPDATED:
            {
                stopMining();
                stopBlockchainMonitoring();
                BlockMiningParameters params = requestMiningParameters();
                adjustBlockTemplate(params.blockTemplate);
                startBlockchainMonitoring();
                startMining(params);
                break;
            }
        }
    }
}

MinerEvent MinerManager::waitEvent()
{
    while(m_events.empty())
    {
        m_eventOccurred.wait();
        m_eventOccurred.clear();
    }

    MinerEvent event = std::move(m_events.front());
    m_events.pop();

    return event;
}

void MinerManager::pushEvent(MinerEvent&& event)
{
    m_events.push(std::move(event));
    m_eventOccurred.set();
}

void MinerManager::startMining(const CryptoNote::BlockMiningParameters& params)
{
    m_contextGroup.spawn([this, params] ()
    {
        try
        {
            m_minedBlock = m_miner.mine(params, m_config.threadCount);
            pushEvent(BlockMinedEvent());
        }
        catch (const std::exception &)
        {
        }
    });
}

void MinerManager::stopMining()
{
    m_miner.stop();
}

void MinerManager::startBlockchainMonitoring()
{
    m_contextGroup.spawn([this] ()
    {
        try
        {
            m_blockchainMonitor.waitBlockchainUpdate();
            pushEvent(BlockchainUpdatedEvent());
        }
        catch (const std::exception &)
        {
        }
    });
}

void MinerManager::stopBlockchainMonitoring()
{
    m_blockchainMonitor.stop();
}

bool MinerManager::submitBlock(const BlockTemplate& minedBlock)
{
    CachedBlock cachedBlock(minedBlock);

    rapidjson::StringBuffer string_buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(string_buffer);

    writer.StartObject();
    writer.Key("jsonrpc");
    writer.String("2.0");
    writer.Key("method");
    writer.String("submitblock");
    writer.Key("params");
    writer.StartArray();
    writer.String(Common::toHex(toBinaryArray(minedBlock)));
    writer.EndArray();
    writer.EndObject();

    auto res = m_httpClient->Post("/json_rpc", string_buffer.GetString(), "application/json");

    if (!res || res->status == 200)
    {
        std::cout << SuccessMsg("\nBlock found! Hash: ")
                  << SuccessMsg(cachedBlock.getBlockHash()) << "\n\n";

        return true;
    }
    else
    {
        std::cout << WarningMsg("Failed to submit block, possibly daemon offline or syncing?\n");
        return false;
    }
}

BlockMiningParameters MinerManager::requestMiningParameters()
{
    while (true)
    {
        rapidjson::StringBuffer string_buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(string_buffer);

        writer.StartObject();
        writer.Key("jsonrpc");
        writer.String("2.0");
        writer.Key("method");
        writer.String("submitblock");
        writer.Key("params");
        writer.StartObject();
        writer.Key("wallet_address");
        writer.String(m_config.miningAddress);
        writer.Key("reserve_size");
        writer.Uint(0);
        writer.EndObject();
        writer.EndObject();

        auto res = m_httpClient->Post("/json_rpc", string_buffer.GetString(), "application/json");

        if (!res)
        {
            std::cout << WarningMsg("Failed to get block template - Is your daemon open?\n");

            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        if (res->status != 200)
        {
            std::stringstream stream;

            stream << "Failed to get block template - received unexpected http "
                   << "code from server: "
                   << res->status << std::endl;

            std::cout << WarningMsg(stream.str()) << std::endl;

            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        rapidjson::Document j;
        j.Parse(res->body);
        if (!j.HasParseError()) {
            const std::string status = j["result"]["status"].GetString();

            if (status != "OK") {
                std::stringstream stream;
                stream  << "Failed to get block hash from daemon. Response: "
                        << status << std::endl;
                std::cout << WarningMsg(stream.str());
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            BlockMiningParameters params;
            params.difficulty = j["result"]["difficulty"].GetUint64();

            std::vector<uint8_t> blob = Common::fromHex(
                j["result"]["blocktemplate_blob"].GetString()
            );

            if(!fromBinaryArray(params.blockTemplate, blob)) {
                std::cout << WarningMsg("Couldn't parse block template from daemon.") << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            return params;
        } else {
            std::stringstream stream;
            stream << "Failed to parse block hash from daemon. Received data:\n"
                << res->body << "\nParse error: " << GetParseError_En(j.GetParseError()) 
                << std::endl;
            
            std::cout << WarningMsg(stream.str());
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
    }
}

void MinerManager::adjustBlockTemplate(CryptoNote::BlockTemplate& blockTemplate) const
{
    adjustMergeMiningTag(blockTemplate);

    if (m_config.firstBlockTimestamp == 0)
    {
        /* no need to fix timestamp */
        return;
    }

    if (m_lastBlockTimestamp == 0)
    {
        blockTemplate.timestamp = m_config.firstBlockTimestamp;
    }
    else if (m_lastBlockTimestamp != 0 && m_config.blockTimestampInterval != 0)
    {
        blockTemplate.timestamp = m_lastBlockTimestamp + m_config.blockTimestampInterval;
    }
}

} //namespace Miner
