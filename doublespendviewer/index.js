var trimHash = function(s) {
    return s.substr(0,4) + "..." + s.substr(s.length-4,4);
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
                spend.append($('<td>').text(s.mainChainBlock.height));
                spend.append($('<td>').text(s.mainChainTx.txid));

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
                var txs = $("<a>").attr("href","#").text(doubleSpendTxes.length);
                txs.click(s, (event) => {

                    var showSpend = event.data;

                    $('#outpointdetails').empty();
                    var doubleSpendTxes2 = [];
                    for(dso of showSpend.doubleSpentOutpoints) {
                        if(!doubleSpendTxes2.find(dstxid => (dstxid === dso.alsoSpentIn.tx.txid))) {
                            doubleSpendTxes2.push(dso.alsoSpentIn.tx.txid)

                            var dsoRow = $('<tr>');
                            var dsoValue = 0;
                                
                            for(out of dso.alsoSpentIn.tx.vout) {
                                dsoValue += out.valueSat/100000000;
                            }

                            dsoRow.append($("<td>").text(trimHash(dso.alsoSpentIn.block.hash)));
                            dsoRow.append($("<td>").text(trimHash(dso.alsoSpentIn.tx.txid)));
                            dsoRow.append($("<td>").text(dsoValue));

                            var dsoRowOutpoints = $("<td>");
                            for(dso2 of showSpend.doubleSpentOutpoints) {
                                if(dso2.alsoSpentIn.tx.txid == dso.alsoSpentIn.tx.txid) {
                                    dsoRowOutpoints.append($("<div>").text(trimHash(dso2.outpoint)));
                                }
                            }
                            dsoRow.append(dsoRowOutpoints);
                            $('#outpointdetails').append(dsoRow);
                        }
                    }
                    $('#detailTxes').modal();
                });
                spend.append($('<td>').append(txs));
                spend.append($('<td>').text(doubleSpendOutputValue));
                $('#spends').append(spend);
            };

        }
      });
    

    
});

