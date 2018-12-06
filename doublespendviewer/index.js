var trimHash = function(s, z) {
    z = z || 4
    return s.substr(0,z) + "..." + s.substr(s.length-z,z);
}

var zeroPad = function(n, width) {
    z = '0';
    n = n + '';
    return n.length >= width ? n : new Array(width - n.length + 1).join(z) + n;
}

var getBlockHeightFromIncident = function(incident) {
    if(incident.event === "doubleSpend") { 
        return incident.details.mainChainBlock.height;
    }
    if(incident.event === "spendingReorgedCoinbase") {
        return incident.details.orphanedBlock.height;
    }
}

var sortIncidents = function(incidentA, incidentB) {
    return ((incidentA < incidentB) ? -1 : ((incidentA > incidentB) ? 1 : 0));
}

$(document).ready(() => {
    $.ajax({
        dataType: "json",
        url: "doublespends.json",
        success: (events) => {
            $('#numSpends').text(events.length);
            events.sort(sortIncidents);
            for(s of events) {
                var spend = $('<tr>');
                
                var type = "";
                if(s.event === "doubleSpend") { 
                    type = "Double Spend";
                }
                if(s.event === "spendingReorgedCoinbase") {
                    type = "Spent Lost Coinbase";
                }

                spend.append($('<td>').append($("<span>").addClass("badge badge-pill badge-secondary").text(type)));
                var txs;

                if(s.event === "doubleSpend") {
                    var blockHashLink = $('<a>').attr('target','_blank').attr('href','http://insight.vertcoin.org/block/' + s.details.mainChainBlock.hash).text(trimHash(s.details.mainChainBlock.hash) + " (" + s.details.mainChainBlock.height + ")");
                    var txidLink = $('<a>').attr('target','_blank').attr('href','http://insight.vertcoin.org/tx/' + s.details.mainChainTx.txid).text(trimHash(s.details.mainChainTx.txid,8));
                    spend.append($('<td>').append(blockHashLink));
                    spend.append($('<td>').append(txidLink));
                    spend.append($('<td>').append($('<a>').attr('href','#').text(trimHash(s.details.doubleSpentOutpoints[0].alsoSpentIn.block.hash) + " (" + s.details.doubleSpentOutpoints[0].alsoSpentIn.block.height + ")").click(s.details.doubleSpentOutpoints[0].alsoSpentIn.block.hash, (event) => { alert(event.data); })));
                    txs = $("<span>").text(trimHash(s.details.doubleSpentOutpoints[0].alsoSpentIn.tx.txid,8));
                } else if(s.event === "spendingReorgedCoinbase") {
                    spend.append($('<td>').text(" "));
                    spend.append($('<td>').text(" "));
                    spend.append($('<td>').append($('<a>').attr('href','#').text(trimHash(s.details.orphanedBlock.hash) + " (" + s.details.orphanedBlock.height + ")").click(s.details.orphanedBlock.hash, (event) => { alert(event.data); })));
                    txs = $("<span>").text(trimHash(s.details.orphanedTx.txid,8));
                }

                var detailButton = $("<button>").addClass("btn btn-default btn-sm").append($("<i>").addClass("fas fa-search"));            
                detailButton.click(s, (event) => {

                    var showSpend = event.data;
                    var block, tx;
                    if(showSpend.event === "doubleSpend") {
                        block = showSpend.details.doubleSpentOutpoints[0].alsoSpentIn.block;
                        tx = showSpend.details.doubleSpentOutpoints[0].alsoSpentIn.tx;
                    } else if(showSpend.event === "spendingReorgedCoinbase") {
                        block = showSpend.details.orphanedBlock;
                        tx = showSpend.details.orphanedTx;
                    }


                    $('#doubleSpendOrphanedBlockHash').text(block.hash);
                    $('#doubleSpendOrphanedBlockHeight').text(block.height);
                    $('#doubleSpendOrphanedTransactionID').text(tx.txid);
                    
                    $('#doubleSpendTransactionInputs').empty();



                    for(vin of tx.vin) {
                        var inputRow = $("<tr>");
                
                        inputRow.append($("<td>").append($('<a>').attr('href','#').text(trimHash(vin.txid, 8)).click(vin.txid, (event) => { alert(event.data); })))
                        inputRow.append($("<td>").text(vin.vout));

                        var matchOutpoint = (vin.txid + zeroPad(vin.vout,8));
                        
                        if(showSpend.event === "doubleSpend") {
                            for(dso of showSpend.details.doubleSpentOutpoints) {
                                if(dso.outpoint === matchOutpoint) {
                                    // This input is a double spend, mark red
                                    inputRow.addClass('table-danger');
                                }
                            }
                        } else if(showSpend.event === "spendingReorgedCoinbase") {
                            for(cbspent of showSpend.details.coinbasesSpent) {
                                if(cbspent === matchOutpoint) {
                                    // This input is a spent reorged coinbase, mark red
                                    inputRow.addClass('table-danger');
                                }
                            }
                        }
                        
                        $('#doubleSpendTransactionInputs').append(inputRow);
                    }

                    $('#doubleSpendTransactionOutputs').empty();
                    for(vout of tx.vout) {
                        var outputRow = $("<tr>");
                
                        var htmlRecipients = "";
                        for(to of vout.to) {
                            if(!(htmlRecipients === "")) { htmlRecipients += "<br/>"; }
                            htmlRecipients += to;
                        }

                        outputRow.append($("<td>").html(htmlRecipients));
                        outputRow.append($("<td>").text((vout.valueSat/100000000).toString() + " VTC"));
                        
                        $('#doubleSpendTransactionOutputs').append(outputRow);
                    }

                    $('#doubleSpendTxDetail').modal();
                });
                spend.append($('<td>').append(txs));
                spend.append($('<td>').append(detailButton));

                var doubleSpendOutputValue = 0;
                if(s.event === "doubleSpend") {
                    var doubleSpendTxes = [];
                    for(dso of s.details.doubleSpentOutpoints) {
                        if(!doubleSpendTxes.find(dstxid => (dstxid === dso.alsoSpentIn.tx.txid))) {
                            doubleSpendTxes.push(dso.alsoSpentIn.tx.txid)

                            
                            for(out of dso.alsoSpentIn.tx.vout) {
                                doubleSpendOutputValue += out.valueSat/100000000;
                            }
                        
                        }
                    }
                } else if (s.event === "spendingReorgedCoinbase") {
                    for(out of s.details.orphanedTx.vout) {
                        doubleSpendOutputValue += out.valueSat/100000000;
                    }
                }

                spend.append($('<td>').text(doubleSpendOutputValue));
                $('#spends').append(spend);
            };

        }
      });
    

    
});
