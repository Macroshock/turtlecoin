// Copyright (c) 2018, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <CryptoNote.h>

#include <JsonHelper.h>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"

#include <unordered_map>
#include <optional>
#include <string>

namespace WalletTypes
{
    struct KeyOutput
    {
        Crypto::PublicKey key;

        uint64_t amount;

        /* Daemon doesn't supply this, blockchain cache api does. */
        std::optional<uint64_t> globalOutputIndex;

        /* Converts the class to a json object */
        void toJSON(rapidjson::Writer<rapidjson::StringBuffer> &writer) const
        {
            writer.StartObject();

            writer.Key("key");
            key.toJSON(writer);

            writer.Key("amount");
            writer.Uint64(amount);

            writer.EndObject();
        }

        /* Initializes the class from a json value */
        void fromJSON(const JSONValue &j) 
        {
            key.fromString(getStringFromJSON(j, "key"));
            amount = getUint64FromJSON(j, "amount");
            /* If we're talking to a daemon or blockchain cache
            that returns the globalIndex as part of the structure
            of a key output, then we need to load that into the
            data structure. */
            if (j.HasMember("globalIndex")) 
            {
                globalOutputIndex = getUint64FromJSON(j, "globalIndex");
            }
        }
    };

    /* A coinbase transaction (i.e., a miner reward, there is one of these in
       every block). Coinbase transactions have no inputs.

       We call this a raw transaction, because it is simply key images and
       amounts */
    struct RawCoinbaseTransaction
    {
        /* The outputs of the transaction, amounts and keys */
        std::vector<KeyOutput> keyOutputs;

        /* The hash of the transaction */
        Crypto::Hash hash;

        /* The public key of this transaction, taken from the tx extra */
        Crypto::PublicKey transactionPublicKey;

        /* When this transaction's inputs become spendable. Some genius thought
           it was a good idea to use this field as both a block height, and a
           unix timestamp. If the value is greater than
           CRYPTONOTE_MAX_BLOCK_NUMBER (In cryptonoteconfig) it is treated
           as a unix timestamp, else it is treated as a block height. */
        uint64_t unlockTime;

        size_t memoryUsage() const
        {
            return keyOutputs.size() * sizeof(KeyOutput) + sizeof(keyOutputs) +
                   sizeof(hash) +
                   sizeof(transactionPublicKey) +
                   sizeof(unlockTime);
        }

        /* Converts the class to a json object */
        void toJSON(rapidjson::Writer<rapidjson::StringBuffer> &writer) const
        {
            writer.StartObject();

            writer.Key("outputs");
            writer.StartArray();
            for (const auto &item : keyOutputs) 
            {
                item.toJSON(writer);
            }
            writer.EndArray();
            
            writer.Key("hash");
            hash.toJSON(writer);

            writer.Key("txPublicKey");
            transactionPublicKey.toJSON(writer);

            writer.Key("unlockTime");
            writer.Uint64(unlockTime);

            writer.EndObject();
        }

        /* Initializes the class from a json value */
        void fromJSON(const JSONValue &j) 
        {
            keyOutputs.clear();

            for (const auto &x : getArrayFromJSON(j, "outputs")) 
            {
                KeyOutput keyOutput;
                keyOutput.fromJSON(x);
                keyOutputs.push_back(keyOutput);
            }
            
            hash.fromString(getStringFromJSON(j, "hash"));
            transactionPublicKey.fromString(getStringFromJSON(j, "txPublicKey"));

            /* We need to try to get the unlockTime from an integer in the json
            however, if that fails because we're talking to a blockchain
            cache API that encodes unlockTime as a string (due to json
            integer encoding limits), we need to attempt this as a string */
            try 
            {
                unlockTime = getUint64FromJSON(j, "unlockTime");
            } 
            catch (const JsonException &e) 
            {
                unlockTime = std::stoull(getStringFromJSON(j, "unlockTime"));
            }
        }
    };

    /* A raw transaction, simply key images and amounts */
    struct RawTransaction : RawCoinbaseTransaction
    {
        /* The transaction payment ID - may be an empty string */
        std::string paymentID;

        /* The inputs used for a transaction, can be used to track outgoing
           transactions */
        std::vector<CryptoNote::KeyInput> keyInputs;

        size_t memoryUsage() const
        {
            return paymentID.size() * sizeof(char) + sizeof(paymentID) +
                   keyInputs.size() * sizeof(CryptoNote::KeyInput) + sizeof(keyInputs) +
                   RawCoinbaseTransaction::memoryUsage();
        }
        
        /* Converts the class to a json object */
        void toJSON(rapidjson::Writer<rapidjson::StringBuffer> &writer) const
        {
            writer.StartObject();

            writer.Key("outputs");
            writer.StartArray();
            for (const auto &item : keyOutputs) 
            {
                item.toJSON(writer);
            }
            writer.EndArray();
            
            writer.Key("hash");
            hash.toJSON(writer);

            writer.Key("txPublicKey");
            transactionPublicKey.toJSON(writer);

            writer.Key("unlockTime");
            writer.Uint64(unlockTime);

            writer.Key("paymentID");
            writer.String(paymentID);

            writer.Key("inputs");
            writer.StartArray();
            for (const auto &item : keyInputs) 
            {
                item.toJSON(writer);
            }
            writer.EndArray();

            writer.EndObject();
        }

        /* Initializes the class from a json value */
        void fromJSON(const JSONValue &j)
        {
            keyOutputs.clear();
            for (const auto &x : getArrayFromJSON(j, "outputs")) 
            {
                KeyOutput keyOutput;
                keyOutput.fromJSON(x);
                keyOutputs.push_back(keyOutput);
            }

            hash.fromString(getStringFromJSON(j, "hash"));
            transactionPublicKey.fromString(getStringFromJSON(j, "txPublicKey"));
            unlockTime = getUint64FromJSON(j, "unlockTime");
            
            /* We need to try to get the unlockTime from an integer in the json
            however, if that fails because we're talking to a blockchain
            cache API that encodes unlockTime as a string (due to json
            integer encoding limits), we need to attempt this as a string */
            try 
            {
                unlockTime = getUint64FromJSON(j, "unlockTime");
            } 
            catch (const JsonException &e) 
            {
                unlockTime = std::stoull(getStringFromJSON(j, "unlockTime"));
            }

            paymentID = getStringFromJSON(j, "paymentID");

            keyInputs.clear();
            for (const auto &x : getArrayFromJSON(j, "outputs")) 
            {
                CryptoNote::KeyInput keyInput;
                keyInput.fromJSON(x);
                keyInputs.push_back(keyInput);
            }
        }
    };

    /* A 'block' with the very basics needed to sync the transactions */
    struct WalletBlockInfo
    {
        /* The coinbase transaction */
        RawCoinbaseTransaction coinbaseTransaction;

        /* The transactions in the block */
        std::vector<RawTransaction> transactions;

        /* The block height (duh!) */
        uint64_t blockHeight;

        /* The hash of the block */
        Crypto::Hash blockHash;

        /* The timestamp of the block */
        uint64_t blockTimestamp;

        size_t memoryUsage() const
        {
            const size_t txUsage = std::accumulate(
                transactions.begin(),
                transactions.end(),
                sizeof(transactions),
                [](const auto acc, const auto item) {

                return acc + item.memoryUsage();
            });
            
            return coinbaseTransaction.memoryUsage() +
                   txUsage +
                   sizeof(blockHeight) +
                   sizeof(blockHash) +
                   sizeof(blockTimestamp);
        }
        
        /* Converts the class to a json object */
        void toJSON(rapidjson::Writer<rapidjson::StringBuffer> &writer) const
        {
            writer.StartObject();
            writer.Key("coinbaseTX");
            coinbaseTransaction.toJSON(writer);

            writer.Key("transactions");
            writer.StartArray();
            for (const auto &tx : transactions) 
            { 
                tx.toJSON(writer);
            }
            writer.EndArray();

            writer.Key("blockHeight");
            writer.Uint64(blockHeight);

            writer.Key("blockHash");
            blockHash.toJSON(writer);

            writer.Key("blockTimestamp");
            writer.Uint64(blockTimestamp);

            writer.EndObject();
        }

        /* Initializes the class from a json value */
        void fromJSON(const JSONValue &j)
        {
            coinbaseTransaction.fromJSON(getJsonValue(j, "transactions"));

            transactions.clear();
            for (const auto &item : getArrayFromJSON(j, "transfers")) 
            {
                RawTransaction tx;
                tx.fromJSON(item);
                transactions.push_back(tx);
            }

            blockHeight = getUint64FromJSON(j, "blockHeight");
            blockHash.fromString(getStringFromJSON(j, "blockHash"));
            blockTimestamp = getUint64FromJSON(j, "blockTimestamp");
        }
    };

    struct TransactionInput
    {
        /* The key image of this amount */
        Crypto::KeyImage keyImage;

        /* The value of this key image */
        uint64_t amount;

        /* The block height this key images transaction was included in
           (Need this for removing key images that were received on a forked
           chain) */
        uint64_t blockHeight;

        /* The transaction public key that was included in the tx_extra of the
           transaction */
        Crypto::PublicKey transactionPublicKey;

        /* The index of this input in the transaction */
        uint64_t transactionIndex;

        /* The index of this output in the 'DB' */
        std::optional<uint64_t> globalOutputIndex;

        /* The transaction key we took from the key outputs */
        Crypto::PublicKey key;

        /* If spent, what height did we spend it at. Used to remove spent
           transaction inputs once they are sure to not be removed from a
           forked chain. */
        uint64_t spendHeight;

        /* When does this input unlock for spending. Default is instantly
           unlocked, or blockHeight + CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW
           for a coinbase/miner transaction. Users can specify a custom
           unlock height however. */
        uint64_t unlockTime;

        /* The transaction hash of the transaction that contains this input */
        Crypto::Hash parentTransactionHash;

        bool operator==(const TransactionInput &other)
        {
            return keyImage == other.keyImage;
        }

        /* Converts the class to a json object */
        void toJSON(rapidjson::Writer<rapidjson::StringBuffer> &writer) const
        {
            writer.StartObject();

            writer.Key("keyImage");
            keyImage.toJSON(writer);

            writer.Key("amount");
            writer.Uint64(amount);

            writer.Key("blockHeight");
            writer.Uint64(blockHeight);

            writer.Key("transactionPublicKey");
            transactionPublicKey.toJSON(writer);

            writer.Key("transactionIndex");
            writer.Uint64(transactionIndex);

            writer.Key("globalOutputIndex");
            writer.Uint64(globalOutputIndex.value_or(0));

            writer.Key("key");
            key.toJSON(writer);

            writer.Key("spendHeight");
            writer.Uint64(spendHeight);

            writer.Key("unlockTime");
            writer.Uint64(unlockTime);

            writer.Key("parentTransactionHash");
            parentTransactionHash.toJSON(writer);

            writer.EndObject();
        }

        /* Initializes the class from a json string */
        void fromJSON(const JSONValue &j)
        {
            keyImage.fromString(getStringFromJSON(j, "keyImage"));
            amount = getUint64FromJSON(j, "amount");
            blockHeight = getUint64FromJSON(j, "blockHeight");
            transactionPublicKey.fromString(getStringFromJSON(j, "transactionPublicKey"));
            transactionIndex = getUint64FromJSON(j, "transactionIndex");
            globalOutputIndex = getUint64FromJSON(j, "globalOutputIndex");
            key.fromString(getStringFromJSON(j, "key"));
            spendHeight = getUint64FromJSON(j, "spendHeight");
            unlockTime = getUint64FromJSON(j, "unlockTime");
            parentTransactionHash.fromString(getStringFromJSON(j, "parentTransactionHash"));
        }
    };

    /* Includes the owner of the input so we can sign the input with the
       correct keys */
    struct TxInputAndOwner
    {
        TxInputAndOwner(
            const TransactionInput input,
            const Crypto::PublicKey publicSpendKey,
            const Crypto::SecretKey privateSpendKey) :

            input(input),
            publicSpendKey(publicSpendKey),
            privateSpendKey(privateSpendKey)
        {
        }

        TransactionInput input;

        Crypto::PublicKey publicSpendKey;

        Crypto::SecretKey privateSpendKey;
    };

    struct TransactionDestination
    {
        /* The public spend key of the receiver of the transaction output */
        Crypto::PublicKey receiverPublicSpendKey;

        /* The public view key of the receiver of the transaction output */
        Crypto::PublicKey receiverPublicViewKey;

        /* The amount of the transaction output */
        uint64_t amount;
    };

    struct GlobalIndexKey
    {
        uint64_t index;
        Crypto::PublicKey key;
    };

    struct ObscuredInput
    {
        /* The outputs, including our real output, and the fake mixin outputs */
        std::vector<GlobalIndexKey> outputs;

        /* The index of the real output in the outputs vector */
        uint64_t realOutput;

        /* The real transaction public key */
        Crypto::PublicKey realTransactionPublicKey;

        /* The index in the transaction outputs vector */
        uint64_t realOutputTransactionIndex;

        /* The amount being sent */
        uint64_t amount;

        /* The owners keys, so we can sign the input correctly */
        Crypto::PublicKey ownerPublicSpendKey;

        Crypto::SecretKey ownerPrivateSpendKey;
    };

    class Transaction
    {
        public:

            //////////////////
            /* Constructors */
            //////////////////

            Transaction() {};

            Transaction(
                /* Mapping of public key to transaction amount, can be multiple
                   if one transaction sends to multiple subwallets */
                const std::unordered_map<Crypto::PublicKey, int64_t> transfers,
                const Crypto::Hash hash,
                const uint64_t fee,
                const uint64_t timestamp,
                const uint64_t blockHeight,
                const std::string paymentID,
                const uint64_t unlockTime,
                const bool isCoinbaseTransaction) :

                transfers(transfers),
                hash(hash),
                fee(fee),
                timestamp(timestamp),
                blockHeight(blockHeight),
                paymentID(paymentID),
                unlockTime(unlockTime),
                isCoinbaseTransaction(isCoinbaseTransaction)
            {
            }

            /////////////////////////////
            /* Public member functions */
            /////////////////////////////

            int64_t totalAmount() const
            {
                int64_t sum = 0;

                for (const auto [pubKey, amount] : transfers)
                {
                    sum += amount;
                }

                return sum;
            }

            /* It's worth noting that this isn't a conclusive check for if a
               transaction is a fusion transaction - there are some requirements
               it has to meet - but we don't need to check them, as the daemon
               will handle that for us - Any transactions that come to the
               wallet (assuming a non malicious daemon) that are zero and not
               a coinbase, is a fusion transaction */
            bool isFusionTransaction() const
            {
                return fee == 0 && !isCoinbaseTransaction;
            }

            /////////////////////////////
            /* Public member variables */
            /////////////////////////////

            /* A map of public keys to amounts, since one transaction can go to
               multiple addresses. These can be positive or negative, for example
               one address might have sent 10,000 TRTL (-10000) to two recipients
               (+5000), (+5000)

               All the public keys in this map, are ones that the wallet container
               owns, it won't store amounts belonging to random people */
            std::unordered_map<Crypto::PublicKey, int64_t> transfers;

            /* The hash of the transaction */
            Crypto::Hash hash;

            /* The fee the transaction was sent with (always positive) */
            uint64_t fee;

            /* The blockheight this transaction is in */
            uint64_t blockHeight;

            /* The timestamp of this transaction (taken from the block timestamp) */
            uint64_t timestamp;

            /* The paymentID of this transaction (will be an empty string if no pid) */
            std::string paymentID;

            /* When does the transaction unlock */
            uint64_t unlockTime;

            /* Was this transaction a miner reward / coinbase transaction */
            bool isCoinbaseTransaction;

            /* Converts the class to a json object */
            void toJSON(rapidjson::Writer<rapidjson::StringBuffer> &writer) const
            {
                writer.StartObject();

                writer.Key("transfers");
                writer.StartArray();
                for (const auto &[publicKey, amount] : transfers)
                {
                    writer.StartObject();

                    writer.Key("publicKey");
                    publicKey.toJSON(writer);

                    writer.Key("amount");
                    writer.Int64(amount);

                    writer.EndObject();
                }
                writer.EndArray();

                writer.Key("hash");
                hash.toJSON(writer);

                writer.Key("fee");
                writer.Uint64(fee);

                writer.Key("blockHeight");
                writer.Uint64(blockHeight);

                writer.Key("timestamp");
                writer.Uint64(timestamp);

                writer.Key("paymentID");
                writer.String(paymentID);

                writer.Key("unlockTime");
                writer.Uint64(unlockTime);

                writer.Key("isCoinbaseTransaction");
                writer.Bool(isCoinbaseTransaction);

                writer.EndObject();
            }

            /* Initializes the class from a json string */
            void fromJSON(const JSONValue &j)
            {
                for (const auto &x : getArrayFromJSON(j, "transfers"))
                {
                    Crypto::PublicKey publicKey;
                    publicKey.fromString(getStringFromJSON(x, "publicKey"));
                    transfers[publicKey] = getInt64FromJSON(x, "amount");
                }

                hash.fromString(getStringFromJSON(j, "hash"));
                fee = getUint64FromJSON(j, "fee");
                blockHeight = getUint64FromJSON(j, "blockHeight");
                timestamp = getUint64FromJSON(j, "timestamp");
                paymentID = getStringFromJSON(j, "paymentID");
                unlockTime = getUint64FromJSON(j, "unlockTime");
                isCoinbaseTransaction = getBoolFromJSON(j, "isCoinbaseTransaction");
            }
    };

    struct WalletStatus
    {
        /* The amount of blocks the wallet has synced */
        uint64_t walletBlockCount;

        /* The amount of blocks the daemon we are connected to has synced */
        uint64_t localDaemonBlockCount;

        /* The amount of blocks the daemons on the network have */
        uint64_t networkBlockCount;

        /* The amount of peers the node is connected to */
        uint32_t peerCount;

        /* The hashrate (based on the last block the daemon has synced) */
        uint64_t lastKnownHashrate;
    };

    /* A structure just used to display locked balance, due to change from
       sent transactions. We just need the amount and a unique identifier
       (hash+key), since we can't spend it, we don't need all the other stuff */
    struct UnconfirmedInput
    {
        /* The amount of the input */
        uint64_t amount;

        /* The transaction key we took from the key outputs */
        Crypto::PublicKey key;

        /* The transaction hash of the transaction that contains this input */
        Crypto::Hash parentTransactionHash;

        /* Converts the class to a json object */
        void toJSON(rapidjson::Writer<rapidjson::StringBuffer> &writer) const
        {
            writer.StartObject();

            writer.Key("amount");
            writer.Uint64(amount);

            writer.Key("key");
            key.toJSON(writer);

            writer.Key("parentTransactionHash");
            parentTransactionHash.toJSON(writer);

            writer.EndObject();
        }

        /* Initializes the class from a json string */
        void fromJSON(const JSONValue &j)
        {
            amount = getUint64FromJSON(j, "amount");
            key.fromString(getStringFromJSON(j, "key"));
            parentTransactionHash.fromString(getStringFromJSON(j, "parentTransactionHash"));
        }
    };
}
