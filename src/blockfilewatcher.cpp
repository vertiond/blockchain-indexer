/*  VTC Blockindexer - A utility to build additional indexes to the 
    Vertcoin blockchain by scanning and indexing the blockfiles
    downloaded by Vertcoin Core.
    
    Copyright (C) 2017  Gert-Jaap Glasbergen

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "blockfilewatcher.h"
#include "scriptsolver.h"
#include "blockchaintypes.h"
#include <iostream>
#include <sstream>
#include <dirent.h>
#include <sys/stat.h>
#include <memory>
#include <iomanip>
#include <unordered_map>
#include "blockscanner.h"

#include <chrono>
#include <thread>
#include <time.h>

using namespace std;
using json = nlohmann::json;

// Constructor
VtcBlockIndexer::BlockFileWatcher::BlockFileWatcher(string blocksDir, const shared_ptr<leveldb::DB> db, const shared_ptr<VtcBlockIndexer::MempoolMonitor> mempoolMonitor) {
    this->db = db;
    this->mempoolMonitor = mempoolMonitor;
    blockIndexer.reset(new VtcBlockIndexer::BlockIndexer(this->db, this->mempoolMonitor));
    blockReader.reset(new VtcBlockIndexer::BlockReader(blocksDir));
    this->blocksDir = blocksDir;
    this->maxLastModified.tv_sec = 0;
    this->maxLastModified.tv_nsec = 0;
    this->scriptSolver = make_unique<VtcBlockIndexer::ScriptSolver>();
}

void VtcBlockIndexer::BlockFileWatcher::startWatcher() {
    DIR *dir;
    dirent *ent;
    string blockFilePrefix = "blk"; 
    
    while(true) {
        bool shouldUpdate = false;
        dir = opendir(&*this->blocksDir.begin());
        while ((ent = readdir(dir)) != NULL) {
            const string file_name = ent->d_name;
            struct stat result;

            // Check if the filename starts with "blk"
            if(strncmp(file_name.c_str(), blockFilePrefix.c_str(), blockFilePrefix.size()) == 0)
            {
                stringstream fullPath;
                fullPath << this->blocksDir << "/" << file_name;
                if(stat(fullPath.str().c_str(), &result)==0)
                {
                    if(result.st_mtim.tv_sec > this->maxLastModified.tv_sec) {
                        this->maxLastModified = result.st_mtim;
                        if(!shouldUpdate)
                            cout << "Change(s) detected, starting index update." << endl;
                        shouldUpdate = true;
                    }
                }
            }
        }

        closedir(dir);

        if(shouldUpdate) { 
            updateIndex();
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void VtcBlockIndexer::BlockFileWatcher::scanBlocks(string fileName) {
    unique_ptr<VtcBlockIndexer::BlockScanner> blockScanner(new VtcBlockIndexer::BlockScanner(blocksDir, fileName));
    if(blockScanner->open())
    {
        while(blockScanner->moveNext()) {
            this->totalBlocks++;
            VtcBlockIndexer::ScannedBlock block = blockScanner->scanNextBlock();
            // Create an empty vector inside the unordered map if this previousBlockHash
            // was not found before.
            if(this->blocks.find(block.previousBlockHash) == this->blocks.end()) {
                this->blocks[block.previousBlockHash] = {};
            }

            // Check if a block with the same hash already exists. Unfortunately, I found
            // instances where a block is included in the block files more than once.
            vector<VtcBlockIndexer::ScannedBlock> matchingBlocks = this->blocks[block.previousBlockHash];
            bool blockFound = false;
            for(VtcBlockIndexer::ScannedBlock matchingBlock : matchingBlocks) {
                if(matchingBlock.blockHash == block.blockHash) {
                    blockFound = true;
                }
            }

            // If the block is not present, add it to the vector.
            if(!blockFound) {
                this->blocks[block.previousBlockHash].push_back(block);
            }
        }
        blockScanner->close();
    }
}


void VtcBlockIndexer::BlockFileWatcher::scanBlockFiles(string dirPath) {
    DIR *dir;
    dirent *ent;

    dir = opendir(&*dirPath.begin());
    while ((ent = readdir(dir)) != NULL) {
        const string file_name = ent->d_name;

        // Check if the filename starts with "blk"
        string prefix = "blk"; 
        if(strncmp(file_name.c_str(), prefix.c_str(), prefix.size()) == 0)
        {
            scanBlocks(file_name);
        }
    }
    closedir(dir);
}


VtcBlockIndexer::ScannedBlock VtcBlockIndexer::BlockFileWatcher::findLongestChain(vector<VtcBlockIndexer::ScannedBlock> matchingBlocks) {
    vector<string> nextBlockHashes;
    for(uint i = 0; i < matchingBlocks.size(); i++) {
        nextBlockHashes.push_back(matchingBlocks.at(i).blockHash);
    }

    while(true) {
       
        for(uint i = 0; i < nextBlockHashes.size(); i++) {
            int countChains = 0;
            for(uint i = 0; i < nextBlockHashes.size(); i++) {
                if(nextBlockHashes.at(i) != "") {
                    countChains++;
                } 
            }
    
            if(countChains == 1) {
                for(uint i = 0; i < nextBlockHashes.size(); i++) {
                    if(nextBlockHashes.at(i) != "") {
                        return matchingBlocks.at(i);
                    } 
                }
            }

            if(this->blocks.find(nextBlockHashes.at(i)) == this->blocks.end()) {
                nextBlockHashes.at(i).assign("");
            } else {
                vector<VtcBlockIndexer::ScannedBlock> matchingBlocks = this->blocks[nextBlockHashes.at(i)];
                VtcBlockIndexer::ScannedBlock bestBlock = matchingBlocks.at(0);
                if(matchingBlocks.size() > 1) { 
                    bestBlock = findLongestChain(matchingBlocks);
                }
                nextBlockHashes.at(i).assign(bestBlock.blockHash);
            }
        }
    }
}


string VtcBlockIndexer::BlockFileWatcher::processNextBlock(string prevBlockHash) {
    
    
    // If there is no block present with this hash as previousBlockHash, return an empty 
    // string signaling we're at the end of the chain.
    if(this->blocks.find(prevBlockHash) == this->blocks.end()) {
        return "";
    }
    
    // Find the blocks that match
    vector<VtcBlockIndexer::ScannedBlock> matchingBlocks = this->blocks[prevBlockHash];
    
    if(matchingBlocks.size() > 0) {
        VtcBlockIndexer::ScannedBlock bestBlock = matchingBlocks.at(0);
        
        if(matchingBlocks.size() > 1) { 
            bestBlock = findLongestChain(matchingBlocks);
        } 
    
        if(!blockIndexer->hasIndexedBlock(bestBlock.blockHash, this->blockHeight)) {
            VtcBlockIndexer::Block fullBlock = blockReader->readBlock(bestBlock.fileName, bestBlock.filePosition, this->blockHeight, false);
           
            blockIndexer->indexBlock(fullBlock);
        }
        return bestBlock.blockHash;

    } else {
        // Somehow found an empty vector in the unordered_map. This should not happen. 
        // But just in case, returning an empty value here.
        return "";
    }
}

void VtcBlockIndexer::BlockFileWatcher::updateIndex() {
    
    time_t start;
    time(&start);  
   
    this->blockHeight = 0;
    this->totalBlocks = 0;
    cout << "Scanning blocks..." << endl;

    scanBlockFiles(blocksDir);
    
    cout << "Found " << this->totalBlocks << " blocks. Constructing longest chain..." << endl;

    // The blockchain starts with the genesis block that has a zero hash as Previous Block Hash
    string nextBlock = "0000000000000000000000000000000000000000000000000000000000000000";
    string processedBlock = processNextBlock(nextBlock);
    double nextUpdate = 10;
    while(processedBlock != "") {

        // Show progress every 10 seconds
        double seconds = difftime(time(NULL), start);
        if(seconds >= nextUpdate) { 
            nextUpdate += 10;
            cout << "Construction is at height " << this->blockHeight << endl;
        }
        this->blockHeight++;
        nextBlock = processedBlock;
        processedBlock = processNextBlock(nextBlock);
    }

    cout << "Done. Processed " << this->blockHeight << " blocks. Have a nice day." << endl;

    this->blocks.clear();
}

vector<VtcBlockIndexer::ScannedBlock> VtcBlockIndexer::BlockFileWatcher::indexBlocksByHeight(int height, vector<VtcBlockIndexer::ScannedBlock> matchingBlocks, VtcBlockIndexer::ScannedBlock blockOnMainChain) {
    //cout << "Adding " << matchingBlocks.size() << " blocks at height " << height << endl;
    
    vector<VtcBlockIndexer::ScannedBlock> followUpBlocks = {};

    // Create an empty vector inside the unordered map if this previousBlockHash
    // was not found before.
    if(this->blocksByHeight.find(height) == this->blocksByHeight.end()) {
        this->blocksByHeight[height] = {};
    }

    // Check if a block with the same hash already exists. Unfortunately, I found
    // instances where a block is included in the block files more than once.
    vector<VtcBlockIndexer::ScannedBlock> matchingKnownBlocks = this->blocksByHeight[height];
    
    for(VtcBlockIndexer::ScannedBlock matchingBlock : matchingBlocks) {
        if(matchingBlock.blockHash == blockOnMainChain.blockHash) { 
            matchingBlock.mainChain = true;
        }

        bool blockFound = false;
        for(VtcBlockIndexer::ScannedBlock matchingKnownBlock : matchingKnownBlocks) {
            if(matchingBlock.blockHash == matchingKnownBlock.blockHash) {
                blockFound = true;
            }
        }
        // If the block is not present, add it to the vector and crawl further.
        if(!blockFound) {
            this->blocksByHeight[height].push_back(matchingBlock);
            vector<VtcBlockIndexer::ScannedBlock> nextMatchingBlocks = this->blocks[matchingBlock.blockHash];
            for(VtcBlockIndexer::ScannedBlock nextMatchingBlock : nextMatchingBlocks)
                followUpBlocks.push_back(nextMatchingBlock);
        }
    }

    return followUpBlocks;
}

void VtcBlockIndexer::BlockFileWatcher::analyzeDoubleBlocks(unordered_map<int, vector<VtcBlockIndexer::Block>> doubleBlocks, json& results, vector<string>& reorgedCoinbases) {

    unordered_map<string, vector<VtcBlockIndexer::PotentialDoubleSpend>> potentialDoubleSpends;

    for(int i = 0; (doubleBlocks.find(i) != doubleBlocks.end()); i++)
    {
        for(int j = 0; j < doubleBlocks[i].size(); j++) {
            VtcBlockIndexer::Block block = doubleBlocks[i][j];
            for(VtcBlockIndexer::Transaction tx : block.transactions) {
                if(tx.inputs.at(0).txHash.compare("0000000000000000000000000000000000000000000000000000000000000000") == 0 && !block.mainChain) {
                    // This is a coinbase transaction that got reorged out. Store its TXID to match spending.
                    // A transaction spending this coinbase will be gone from the main chain after reorg too
                    // without a double spend necessary.
                    reorgedCoinbases.push_back(tx.txHash);
                }

                for(VtcBlockIndexer::TransactionInput txi : tx.inputs) {
                    if(txi.txHash.compare("0000000000000000000000000000000000000000000000000000000000000000") != 0)
                    {
                        stringstream ss;
                        ss << txi.txHash << setw(8) << setfill('0') << txi.txoIndex;
                        VtcBlockIndexer::PotentialDoubleSpend spend;
                        spend.block = block;
                        spend.tx = tx;
                        if(potentialDoubleSpends.find(ss.str()) == potentialDoubleSpends.end()) {
                            potentialDoubleSpends[ss.str()] = { spend };
                        } else {
                            potentialDoubleSpends[ss.str()].push_back(spend);
                        }
                    }
                }
            }
        }
    } 

    vector<DoubleSpend> doubleSpends = {};
    vector<DoubleSpentCoinBase> orphansSpendingReorgedCoinbase = {};
    vector<Transaction> orphansMissingFromMainChain = {};
    for (std::pair<string, vector<VtcBlockIndexer::PotentialDoubleSpend>> potentialDoubleSpend : potentialDoubleSpends)
    {
        VtcBlockIndexer::PotentialDoubleSpend mainChainSpend;
        bool foundMainChainSpend = false;
        for(VtcBlockIndexer::PotentialDoubleSpend spend : potentialDoubleSpend.second) {
            if(spend.block.mainChain) {
                mainChainSpend = spend;
                foundMainChainSpend = true;
                break;
            }
        }

        if(!foundMainChainSpend) {
            for(VtcBlockIndexer::PotentialDoubleSpend spend : potentialDoubleSpend.second) {
                bool spentReorgedCoinbase = false;
                bool spentOtherInputs = false;
                string spentCoinbase;
                for(VtcBlockIndexer::TransactionInput txi : spend.tx.inputs) {
                    bool inputSpendsReorgedCoinbase = false;
                    for(string reorgedCoinbase : reorgedCoinbases) {
                        if(reorgedCoinbase.compare(txi.txHash) == 0) {
                            // This transaction spends a coinbase that was reorged out.
                            inputSpendsReorgedCoinbase = true;
                            spentCoinbase = txi.txHash + "00000000";
                        }
                    }
                    if(inputSpendsReorgedCoinbase) {
                        spentReorgedCoinbase = true;
                    } else { 
                        spentOtherInputs = true;
                    }
                }
                if(spentReorgedCoinbase) {
                    bool foundOrphanSpend = false;
                    for(VtcBlockIndexer::DoubleSpentCoinBase& existingDso : orphansSpendingReorgedCoinbase) {
                        if(existingDso.block.blockHash.compare(spend.block.blockHash) == 0 &&
                            existingDso.tx.txHash.compare(spend.tx.txHash) == 0) {
                            
                            bool foundOutpoint = false;
                            for(string op : existingDso.outpoints) {
                                if(op.compare(spentCoinbase) == 0) {
                                    foundOutpoint = true;
                                }
                            }
                            if(!foundOutpoint) {
                                existingDso.outpoints.push_back(spentCoinbase); 
                            }
                            foundOrphanSpend = true;
                            break;
                        }
                    }
                    if(!foundOrphanSpend) {
                        DoubleSpentCoinBase dspend;
                        dspend.tx = spend.tx;
                        dspend.block = spend.block;
                        dspend.outpoints = {spentCoinbase};
                        orphansSpendingReorgedCoinbase.push_back(dspend);
                    }
                } else {
                    bool foundOrphan = false;
                    for(VtcBlockIndexer::Transaction tx : orphansMissingFromMainChain) {
                        if(spend.tx.txHash.compare(tx.txHash) == 0) {
                            foundOrphan = true;
                        }
                    }
                    if(!foundOrphan) {
                        orphansMissingFromMainChain.push_back(spend.tx);
                    }
                }
            }
            continue;
        }

        if(foundMainChainSpend && potentialDoubleSpend.second.size() > 1) {
            for(VtcBlockIndexer::PotentialDoubleSpend spend : potentialDoubleSpend.second) {
                if(spend.tx.txHash.compare(mainChainSpend.tx.txHash) != 0 &&
                    spend.block.blockHash.compare(mainChainSpend.block.blockHash) != 0) {
                    VtcBlockIndexer::DoubleSpentOutpoint dso;
                    dso.outpoint = potentialDoubleSpend.first;
                    dso.alsoSpentInTx = spend.tx;
                    dso.alsoSpentInBlock = spend.block;
                    bool found = false;
                    for(VtcBlockIndexer::DoubleSpend& existingDspend : doubleSpends) {
                        if(found) break;
                        if(existingDspend.block.blockHash.compare(mainChainSpend.block.blockHash) == 0 &&
                            existingDspend.tx.txHash.compare(mainChainSpend.tx.txHash) == 0) { 
                            for(VtcBlockIndexer::DoubleSpentOutpoint& existingDso : existingDspend.outpoints) {
                                if(existingDso.alsoSpentInBlock.blockHash.compare(dso.alsoSpentInBlock.blockHash) == 0 &&
                                    existingDso.alsoSpentInTx.txHash.compare(dso.alsoSpentInTx.txHash) == 0) {
                                    existingDspend.outpoints.push_back(dso); 
                                    found = true;
                                    break;
                                }
                            }
                        }
                    }
                    
                    if(!found) {
                        DoubleSpend dspend;
                        dspend.block = mainChainSpend.block;
                        dspend.tx = mainChainSpend.tx;
                        dspend.outpoints = {dso};
                        doubleSpends.push_back(dspend);
                    } 
                }
            } 
        }
    }
    

    for(DoubleSpend ds : doubleSpends) {
        json jsonEvent;
        json jsonSpend;
        json jsonSpendBlock;

        jsonSpendBlock["hash"] = ds.block.blockHash;
        jsonSpendBlock["height"] = ds.block.height;
        jsonSpend["mainChainBlock"] = jsonSpendBlock;
        jsonSpend["mainChainTx"] = txToJson(ds.tx); 

        json jdsos = json::array();
        for(DoubleSpentOutpoint dso : ds.outpoints) {
            json jdso;
            jdso["outpoint"] = dso.outpoint;

            json alsoSpentIn;
            json alsoSpentInBlock;
            alsoSpentIn["tx"] = txToJson(dso.alsoSpentInTx);
            alsoSpentInBlock["hash"] = dso.alsoSpentInBlock.blockHash;
            alsoSpentInBlock["height"] = dso.alsoSpentInBlock.height;
            alsoSpentIn["block"] = alsoSpentInBlock;
            jdso["alsoSpentIn"] = alsoSpentIn;
            jdsos.push_back(jdso);
        }
        jsonSpend["doubleSpentOutpoints"] = jdsos;

        jsonEvent["event"] = "doubleSpend";
        jsonEvent["details"] = jsonSpend;
        results.push_back(jsonEvent);
    }

    for(DoubleSpentCoinBase dspend : orphansSpendingReorgedCoinbase) {
        json jsonEvent;
        jsonEvent["event"] = "spendingReorgedCoinbase";
        
        json jsonDetails;

        jsonDetails["orphanedTx"] = txToJson(dspend.tx);

        json jsonBlock;
        jsonBlock["hash"] = dspend.block.blockHash;
        jsonBlock["height"] = dspend.block.height;
        jsonDetails["orphanedBlock"] = jsonBlock;
        
        jsonDetails["coinbasesSpent"] = dspend.outpoints;
        jsonEvent["details"] = jsonDetails;
        results.push_back(jsonEvent);
    }

    /*for(Transaction tx : orphansMissingFromMainChain) {
        json jsonEvent;
        jsonEvent["event"] = "missingFromMainChain";
        jsonEvent["details"] = txToJson(tx);
        results.push_back(jsonEvent);
    }*/


}

json VtcBlockIndexer::BlockFileWatcher::txToJson(VtcBlockIndexer::Transaction tx) { 
    json jtx;
    jtx["txid"] = tx.txHash;

    json vins = json::array();
    for (VtcBlockIndexer::TransactionInput txi : tx.inputs) {
            json vin;
            vin["txid"] = txi.txHash;
            vin["vout"] = txi.txoIndex;
            vins.push_back(vin);
    }
    jtx["vin"] = vins;
    
    json vouts = json::array();
    for (VtcBlockIndexer::TransactionOutput txo : tx.outputs) {
        json vout;

        json scriptPubKey;
        vout["to"] = json::array();
        vector<string> addresses = scriptSolver->getAddressesFromScript(txo.script);
        for(string address : addresses) {
            vout["to"].push_back(address);
        }
        vout["type"] = scriptSolver->getScriptTypeName(txo.script);
        vout["valueSat"] = txo.value;
        
        vouts.push_back(vout);
    }
    jtx["vout"] = vouts;
    return jtx;
}

void VtcBlockIndexer::BlockFileWatcher::dumpDoubleSpends() {
    
    scanBlockFiles(blocksDir);
    
    
    string nextBlock = "0000000000000000000000000000000000000000000000000000000000000000";
    vector<VtcBlockIndexer::ScannedBlock> matchingBlocks = this->blocks[nextBlock];
    int i = 0;
    while(matchingBlocks.size() > 0) {
        i++;
        VtcBlockIndexer::ScannedBlock blockOnMainChain = findLongestChain(matchingBlocks);
        
        matchingBlocks = indexBlocksByHeight(i, matchingBlocks, blockOnMainChain);
    }

    
    json doubleSpends = json::array();

    unordered_map<int, vector<VtcBlockIndexer::Block>> doubleBlocks;
    vector<string> reorgedCoinbases;
    int prevDoubleBlock = -1;
    for(int i = 1; (this->blocksByHeight.find(i) != this->blocksByHeight.end()); i++)
    {
        if(this->blocksByHeight[i].size() > 1) {
            if (prevDoubleBlock != i-1) {
                analyzeDoubleBlocks(doubleBlocks, doubleSpends, reorgedCoinbases);
                doubleBlocks.clear();
            }

            for(int j = 0; j < this->blocksByHeight[i].size(); j++)
            {
                VtcBlockIndexer::ScannedBlock scannedBlock = this->blocksByHeight[i][j];
                VtcBlockIndexer::Block block = blockReader->readBlock(scannedBlock.fileName, scannedBlock.filePosition, i, false);
                block.mainChain = scannedBlock.mainChain;
                if(doubleBlocks.find(j) == doubleBlocks.end()) {
                    doubleBlocks[j] = {block};
                } else {
                    for(int k = 0; doubleBlocks.find(k) != doubleBlocks.end(); k++)
                    {
                        if(doubleBlocks[k][doubleBlocks[k].size()-1].blockHash == block.previousBlockHash) {
                            doubleBlocks[k].push_back(block);
                        }
                    }
                }
            }
            
            prevDoubleBlock = i;
        }
    }

    analyzeDoubleBlocks(doubleBlocks, doubleSpends, reorgedCoinbases);

    cout << doubleSpends << endl;
}
