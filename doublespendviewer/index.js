var trimHash = function(s, z) {
    z = z || 4
    return s.substr(0,z) + "..." + s.substr(s.length-z,z);
}

var zeroPad = function(n, width) {
    z = '0';
    n = n + '';
    return n.length >= width ? n : new Array(width - n.length + 1).join(z) + n;
}

$(document).ready(() => {
    $.ajax({
        dataType: "json",
        url: "doublespends.json",
        success: (spends) => {
            $('#numSpends').text(spends.length);
            spends = spends.reverse();
            for(s of spends) {
                var spend = $('<tr>');
                
                var blockHashLink = $('<a>').attr('target','_blank').attr('href','http://insight.vertcoin.org/block/' + s.mainChainBlock.hash).text(trimHash(s.mainChainBlock.hash) + " (" + s.mainChainBlock.height + ")");
                var txidLink = $('<a>').attr('target','_blank').attr('href','http://insight.vertcoin.org/tx/' + s.mainChainTx.txid).text(trimHash(s.mainChainTx.txid,8));

                spend.append($('<td>').append(blockHashLink));
                spend.append($('<td>').append(txidLink));

                var doubleSpendOutputValue = 0;
                var doubleSpendTxes = [];
                for(dso of s.doubleSpentOutpoints) {
                    if(!doubleSpendTxes.find(dstxid => (dstxid === dso.alsoSpentIn.tx.txid))) {
                        doubleSpendTxes.push(dso.alsoSpentIn.tx.txid)

                        
                        for(out of dso.alsoSpentIn.tx.vout) {
                            doubleSpendOutputValue += out.valueSat/100000000;
                        }
                       
                    }
                }

                var doubleSpendBlockHashLink = $('<a>').attr('href','#').text(trimHash(s.doubleSpentOutpoints[0].alsoSpentIn.block.hash) + " (" + s.doubleSpentOutpoints[0].alsoSpentIn.block.height + ")").click(s.doubleSpentOutpoints[0].alsoSpentIn.block.hash, (event) => { alert(event.data); });
                spend.append($('<td>').append(doubleSpendBlockHashLink));

                var txs = $("<a>").attr("href","#").text(trimHash(s.doubleSpentOutpoints[0].alsoSpentIn.tx.txid,8));
                txs.click(s, (event) => {

                    var showSpend = event.data;

                    $('#doubleSpendOrphanedBlockHash').text(showSpend.doubleSpentOutpoints[0].alsoSpentIn.block.hash);
                    $('#doubleSpendOrphanedBlockHeight').text(showSpend.doubleSpentOutpoints[0].alsoSpentIn.block.height);
                    $('#doubleSpendOrphanedTransactionID').text(showSpend.doubleSpentOutpoints[0].alsoSpentIn.tx.txid);
                    
                    $('#doubleSpendTransactionInputs').empty();
                    for(vin of showSpend.doubleSpentOutpoints[0].alsoSpentIn.tx.vin) {
                        var inputRow = $("<tr>");
                
                        inputRow.append($("<td>").append($('<a>').attr('href','#').text(trimHash(vin.txid, 8)).click(vin.txid, (event) => { alert(event.data); })))
                        inputRow.append($("<td>").text(vin.vout));

                        var matchOutpoint = (vin.txid + zeroPad(vin.vout,8));
                        
                        for(dso of showSpend.doubleSpentOutpoints) {
                            if(dso.outpoint === matchOutpoint) {
                                // This input is a double spend, mark red
                                inputRow.addClass('table-danger');
                            }
                        }
                        $('#doubleSpendTransactionInputs').append(inputRow);
                    }

                    $('#doubleSpendTransactionOutputs').empty();
                    for(vout of showSpend.doubleSpentOutpoints[0].alsoSpentIn.tx.vout) {
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
                spend.append($('<td>').text(doubleSpendOutputValue));
                $('#spends').append(spend);
            };

        }
      });
    

    
});
