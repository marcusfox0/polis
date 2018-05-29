#!/usr/bin/env python3
# Copyright (c) 2015-2018 The Dash Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test deterministic masternodes
#

from test_framework.blocktools import create_block, create_coinbase, get_masternode_payment
from test_framework.mininode import CTransaction, ToHex, FromHex, CTxOut, COIN, CCbTx
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

class Masternode(object):
    pass

class DIP3Test(BitcoinTestFramework):
    def __init__(self):
        super().__init__()
        self.num_initial_mn = 11 # Should be >= 11 to make sure quorums are not always the same MNs
        self.num_nodes = 1 + self.num_initial_mn + 2 # +1 for controller, +1 for mn-qt, +1 for mn created after dip3 activation
        self.setup_clean_chain = True
        try:
            shutil.rmtree('/tmp/asd')
        except:
            pass

        self.extra_args = ["-budgetparams=240:100:240"]
        self.extra_args += ["-sporkaddr=yebBV414nM8rqS8JjeisXuomoKDfpi4J6N", "-sporkkey=cP57a9nyCZgtFAR5buK5y4KUp6NJ3Yj7KtW7zX55LPSFkTHWNkoz"]

    def setup_network(self):
        disable_mocktime()
        self.start_controller_node()
        self.is_network_split = False

    def start_controller_node(self, extra_args=None):
        print("starting controller node")
        if self.nodes is None:
            self.nodes = [None]
        args = self.extra_args
        if extra_args is not None:
            args += extra_args
        self.nodes[0] = start_node(0, self.options.tmpdir, extra_args=args)
        for i in range(1, self.num_nodes):
            if i < len(self.nodes) and self.nodes[i] is not None:
                connect_nodes_bi(self.nodes, 0, i)

    def stop_controller_node(self):
        print("stopping controller node")
        stop_node(self.nodes[0], 0)

    def restart_controller_node(self):
        self.stop_controller_node()
        self.start_controller_node()

    def run_test(self):
        print("funding controller node")
        while self.nodes[0].getbalance() < (self.num_initial_mn + 3) * 1000:
            self.nodes[0].generate(1) # generate enough for collaterals
        print("controller node has {} dash".format(self.nodes[0].getbalance()))

        # Make sure we're below block 143 (which activates dip3)
        print("testing rejection of ProTx before dip3 activation")
        assert(self.nodes[0].getblockchaininfo()['blocks'] < 143)
        dip3_deployment = self.nodes[0].getblockchaininfo()['bip9_softforks']['dip0003']
        assert_equal(dip3_deployment['status'], 'defined')

        self.test_fail_create_protx(self.nodes[0])

        mns = []
        mn_idx = 1
        for i in range(self.num_initial_mn):
            mn = self.create_mn(self.nodes[0], mn_idx, 'mn-%d' % (mn_idx))
            mn_idx += 1
            mns.append(mn)

        # mature collaterals
        for i in range(3):
            self.nodes[0].generate(1)
            time.sleep(1)

        self.write_mnconf(mns)

        self.restart_controller_node()
        for mn in mns:
            self.start_mn(mn)
        self.sync_all()

        # force finishing of mnsync
        for node in self.nodes:
            self.force_finish_mnsync(node)

        # start MNs
        print("start mns")
        for mn in mns:
            self.start_alias(self.nodes[0], mn.alias)
        print("wait for MNs to appear in MN lists")
        self.wait_for_mnlists(mns, True, False)

        print("testing MN payment votes")
        self.test_mn_votes(10)

        print("testing instant send")
        self.test_instantsend(10, 5)

        print("testing rejection of ProTx before dip3 activation (in states defined, started and locked_in)")
        while self.nodes[0].getblockchaininfo()['bip9_softforks']['dip0003']['status'] == 'defined':
            self.nodes[0].generate(1)
        self.test_fail_create_protx(self.nodes[0])
        while self.nodes[0].getblockchaininfo()['bip9_softforks']['dip0003']['status'] == 'started':
            self.nodes[0].generate(1)
        self.test_fail_create_protx(self.nodes[0])

        # prepare mn which should still be accepted later when dip3 activates (because it is funded before final activation)
        print("creating collateral for mn-before-dip3")
        before_dip3_mn = self.create_mn(self.nodes[0], mn_idx, 'mn-before-dip3')
        mn_idx += 1

        while self.nodes[0].getblockchaininfo()['bip9_softforks']['dip0003']['status'] == 'locked_in':
            self.nodes[0].generate(1)

        # We have hundreds of blocks to sync here, give it more time
        print("syncing blocks for all nodes")
        sync_blocks(self.nodes, timeout=120)

        # DIP3 has activated here

        print("testing rejection of ProTx right before dip3 activation")
        best_block = self.nodes[0].getbestblockhash()
        self.nodes[0].invalidateblock(best_block)
        self.test_fail_create_protx(self.nodes[0])
        self.nodes[0].reconsiderblock(best_block)

        # Now it should be possible to mine ProTx
        self.sync_all()
        self.test_success_create_protx(self.nodes[0])

        print("creating collateral for mn-after-dip3")
        after_dip3_mn = self.create_mn(self.nodes[0], mn_idx, 'mn-after-dip3')
        # mature collaterals
        for i in range(3):
            self.nodes[0].generate(1)
            time.sleep(1)

        print("testing if we can start a mn which was created before dip3 activation")
        mns.append(before_dip3_mn)
        self.write_mnconf(mns + [after_dip3_mn])
        self.restart_controller_node()
        self.force_finish_mnsync(self.nodes[0])

        print("start MN %s" % before_dip3_mn.alias)
        self.start_mn(before_dip3_mn)
        self.force_finish_mnsync_list(before_dip3_mn.node)
        self.start_alias(self.nodes[0], before_dip3_mn.alias)

        self.wait_for_mnlists(mns, True, False)
        self.wait_for_mnlists_same()

        # Test if nodes deny creating new non-ProTx MNs now
        print("testing if MN start fails when using collateral which was created after dip3 activation")
        self.start_alias(self.nodes[0], after_dip3_mn.alias, should_fail=True)

        first_upgrade_count = 5
        print("upgrading first %d MNs to use ProTx (but not deterministic MN lists)" % first_upgrade_count)
        for i in range(first_upgrade_count):
            mns[i] = self.upgrade_mn_protx(mns[i])
            self.nodes[0].generate(1)
        self.write_mnconf(mns)

        print("wait for upgraded MNs to disappear from MN lists (their collateral was spent)")
        self.wait_for_mnlists(mns, True, False, check=True)
        self.wait_for_mnlists_same()

        print("restarting controller and upgraded MNs")
        self.restart_controller_node()
        self.force_finish_mnsync_list(self.nodes[0])
        for mn in mns:
            if mn.is_protx:
                print("restarting MN %s" % mn.alias)
                self.stop_node(mn.idx)
                self.start_mn(mn)
                self.force_finish_mnsync_list(mn.node)
        print('start-alias on upgraded nodes')
        for mn in mns:
            if mn.is_protx:
                self.start_alias(self.nodes[0], mn.alias)

        print("wait for upgraded MNs to appear in MN list")
        self.wait_for_mnlists(mns, True, True)
        self.wait_for_mnlists_same()

        print("testing MN payment votes (with mixed ProTx and legacy nodes)")
        self.test_mn_votes(10, test_enforcement=True)

        print("testing instant send (with mixed ProTx and legacy nodes)")
        self.test_instantsend(10, 5)

        print("activating spork15")
        height = self.nodes[0].getblockchaininfo()['blocks']
        spork15_offset = 10
        self.nodes[0].spork('SPORK_15_DETERMINISTIC_MNS_ENABLED', height + spork15_offset)
        self.wait_for_sporks()

        print("test that MN list does not change before final spork15 activation")
        for i in range(spork15_offset - 1):
            self.nodes[0].generate(1)
            self.sync_all()
            self.wait_for_mnlists(mns, True, True)
            self.wait_for_mnlists_same()

        print("mining final block which should switch network to deterministic lists")
        self.nodes[0].generate(1)
        self.sync_all()

        ##### WOW...we made it...we are in deterministic MN lists mode now.
        ##### From now on, we don't wait for mnlists to become correct anymore, we always assert that they are correct immediately

        print("assert that not upgraded MNs disappeared from MN list")
        self.assert_mnlists(mns, False, True)

        # enable enforcement and keep it on from now on
        self.nodes[0].spork('SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT', 0)
        self.wait_for_sporks()

        print("test that MNs disappear from the list when the ProTx collateral is spent")
        spend_mns_count = 3
        mns_tmp = [] + mns
        dummy_txins = []
        for i in range(spend_mns_count):
            dummy_txin = self.spend_mn_collateral(mns[i], with_dummy_input_output=True)
            dummy_txins.append(dummy_txin)
            self.nodes[0].generate(1)
            self.sync_all()
            mns_tmp.remove(mns[i])
            self.assert_mnlists(mns_tmp, False, True)

        print("test that reverting the blockchain on a single node results in the mnlist to be reverted as well")
        for i in range(spend_mns_count):
            self.nodes[0].invalidateblock(self.nodes[0].getbestblockhash())
            mns_tmp.append(mns[spend_mns_count - 1 - i])
            self.assert_mnlist(self.nodes[0], mns_tmp, False, True)

        print("cause a reorg with a double spend and check that mnlists are still correct on all nodes")
        self.mine_double_spend(self.nodes[0], dummy_txins, self.nodes[0].getnewaddress(), use_mnmerkleroot_from_tip=True)
        self.nodes[0].generate(spend_mns_count)
        self.sync_all()
        self.assert_mnlists(mns_tmp, False, True)

        print("upgrade remaining MNs to ProTx")
        for i in range(first_upgrade_count, len(mns)):
            mns[i] = self.upgrade_mn_protx(mns[i])
            mn = mns[i]
            self.nodes[0].generate(1)
            print("restarting MN %s" % mn.alias)
            self.stop_node(mn.idx)
            self.start_mn(mn)
            self.sync_all()
            self.force_finish_mnsync(mn.node)
            self.assert_mnlists(mns, False, True)

        self.assert_mnlists(mns, False, True)

        print("test mn payment enforcement with deterministic MNs")
        for i in range(20):
            node = self.nodes[i % len(self.nodes)]
            self.test_invalid_mn_payment(node)
            node.generate(1)
            self.sync_all()

        print("testing instant send with deterministic MNs")
        self.test_instantsend(20, 5)

        print("testing ProUpServTx")
        for mn in mns:
            self.test_protx_update_service(mn)

    def create_mn(self, node, idx, alias):
        mn = Masternode()
        mn.idx = idx
        mn.alias = alias
        mn.is_protx = False
        mn.p2p_port = p2p_port(mn.idx)

        mn.mnkey = node.masternode('genkey')
        mn.collateral_address = node.getnewaddress()
        mn.collateral_txid = node.sendtoaddress(mn.collateral_address, 1000)
        rawtx = node.getrawtransaction(mn.collateral_txid, 1)

        mn.collateral_vout = -1
        for txout in rawtx['vout']:
            if txout['value'] == Decimal(1000):
                mn.collateral_vout = txout['n']
                break
        assert(mn.collateral_vout != -1)

        lock = node.lockunspent(False, [{'txid': mn.collateral_txid, 'vout': mn.collateral_vout}])

        return mn

    def create_mn_protx(self, node, idx, alias):
        mn = Masternode()
        mn.idx = idx
        mn.alias = alias
        mn.is_protx = True
        mn.p2p_port = p2p_port(mn.idx)

        mn.ownerAddr = node.getnewaddress()
        mn.operatorAddr = mn.ownerAddr
        mn.votingAddr = mn.ownerAddr
        mn.mnkey = node.dumpprivkey(mn.operatorAddr)
        mn.collateral_address = node.getnewaddress()

        mn.collateral_txid = node.protx('register', mn.collateral_address, '1000', '127.0.0.1:%d' % mn.p2p_port, '0', mn.ownerAddr, mn.operatorAddr, mn.votingAddr, 0, mn.collateral_address)
        rawtx = node.getrawtransaction(mn.collateral_txid, 1)

        mn.collateral_vout = -1
        for txout in rawtx['vout']:
            if txout['value'] == Decimal(1000):
                mn.collateral_vout = txout['n']
                break
        assert(mn.collateral_vout != -1)

        return mn

    def start_mn(self, mn):
        while len(self.nodes) <= mn.idx:
            self.nodes.append(None)
        extra_args = ['-masternode=1', '-masternodeprivkey=%s' % mn.mnkey]
        n = start_node(mn.idx, self.options.tmpdir, self.extra_args + extra_args, redirect_stderr=True)
        self.nodes[mn.idx] = n
        for i in range(0, self.num_nodes):
            if i < len(self.nodes) and self.nodes[i] is not None and i != mn.idx:
                connect_nodes_bi(self.nodes, mn.idx, i)
        mn.node = self.nodes[mn.idx]
        self.sync_all()

    def spend_mn_collateral(self, mn, with_dummy_input_output=False):
        return self.spend_input(mn.collateral_txid, mn.collateral_vout, 1000, with_dummy_input_output)

    def upgrade_mn_protx(self, mn):
        self.spend_mn_collateral(mn)
        mn = self.create_mn_protx(self.nodes[0], mn.idx, 'mn-protx-%d' % mn.idx)
        return mn

    def test_protx_update_service(self, mn):
        self.nodes[0].protx('update_service', mn.collateral_txid, '127.0.0.2:%d' % mn.p2p_port, '0')
        self.nodes[0].generate(1)
        self.sync_all()
        for node in self.nodes:
            mn_info = node.masternode('info', mn.collateral_txid)
            mn_list = node.masternode('list')
            assert_equal(mn_info['state']['addr'], '127.0.0.2:%d' % mn.p2p_port)
            assert_equal(mn_list['%s-%d' % (mn.collateral_txid, mn.collateral_vout)]['address'], '127.0.0.2:%d' % mn.p2p_port)

        # undo
        self.nodes[0].protx('update_service', mn.collateral_txid, '127.0.0.1:%d' % mn.p2p_port, '0')
        self.nodes[0].generate(1)

    def force_finish_mnsync(self, node):
        while True:
            s = node.mnsync('next')
            if s == 'sync updated to MASTERNODE_SYNC_FINISHED':
                break
            time.sleep(0.1)

    def force_finish_mnsync_list(self, node):
        if node.mnsync('status')['AssetName'] == 'MASTERNODE_SYNC_WAITING':
            node.mnsync('next')

        while True:
            mnlist = node.masternode('list', 'status')
            if len(mnlist) != 0:
                time.sleep(0.5)
                self.force_finish_mnsync(node)
                return
            time.sleep(0.1)

    def write_mnconf_line(self, mn, f):
        conf_line = "%s %s:%d %s %s %d\n" % (mn.alias, '127.0.0.1', mn.p2p_port, mn.mnkey, mn.collateral_txid, mn.collateral_vout)
        f.write(conf_line)

    def write_mnconf(self, mns):
        mnconf_file = os.path.join(self.options.tmpdir, "node0/regtest/masternode.conf")
        with open(mnconf_file, 'w') as f:
            for mn in mns:
                self.write_mnconf_line(mn, f)

    def start_alias(self, node, alias, should_fail=False):
        start_result = node.masternode('start-alias', alias)
        if not should_fail:
            assert_equal(start_result, {'result': 'successful', 'alias': alias})
        else:
            assert_equal(start_result, {'result': 'failed', 'alias': alias, 'errorMessage': 'Failed to verify MNB'})

    def generate_blocks_until_winners(self, node, count, timeout=60):
        # Winner lists are pretty much messed up when too many blocks were generated in a short time
        # To allow proper testing of winners list, we need to slowly generate a few blocks until the list stabilizes
        good_count = 0
        st = time.time()
        while time.time() < st + timeout:
            height = node.getblockchaininfo()['blocks'] + 10
            winners = node.masternode('winners')
            if str(height) in winners:
                if re.match('[0-9a-zA-Z]*:10', winners[str(height)]):
                    good_count += 1
                    if good_count >= count:
                        return
                else:
                    good_count = 0
            node.generate(1)
            self.sync_all()
            time.sleep(1)
        raise AssertionError("generate_blocks_until_winners timed out: {}".format(node.masternode('winners')))

    def test_mn_votes(self, block_count, test_enforcement=False):
        self.generate_blocks_until_winners(self.nodes[0], self.num_nodes)

        if test_enforcement:
            self.nodes[0].spork('SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT', 0)
            self.wait_for_sporks()
            self.test_invalid_mn_payment(self.nodes[0])

        cur_block = 0
        while cur_block < block_count:
            for n1 in self.nodes:
                if cur_block >= block_count:
                    break
                if n1 is None:
                    continue

                if test_enforcement:
                    self.test_invalid_mn_payment(n1)

                n1.generate(1)
                cur_block += 1
                self.sync_all()

                height = n1.getblockchaininfo()['blocks']
                winners = self.wait_for_winners(n1, height + 10)

                for n2 in self.nodes:
                    if n1 is n2 or n2 is None:
                        continue
                    winners2 = self.wait_for_winners(n2, height + 10)
                    if winners[str(height + 10)] != winners2[str(height + 10)]:
                        print("winner1: " + str(winners[str(height + 10)]))
                        print("winner2: " + str(winners2[str(height + 10)]))
                        raise AssertionError("winners did not match")

        if test_enforcement:
            self.nodes[0].spork('SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT', 4070908800)

    def test_instantsend(self, tx_count, repeat):
        self.nodes[0].spork('SPORK_2_INSTANTSEND_ENABLED', 10000)
        self.wait_for_sporks()

        # give all nodes some coins first
        for i in range(tx_count):
            outputs = {}
            for node in self.nodes[1:]:
                outputs[node.getnewaddress()] = 1
            rawtx = self.nodes[0].createrawtransaction([], outputs)
            rawtx = self.nodes[0].fundrawtransaction(rawtx)['hex']
            rawtx = self.nodes[0].signrawtransaction(rawtx)['hex']
            self.nodes[0].sendrawtransaction(rawtx)
            self.nodes[0].generate(1)
        self.sync_all()

        for j in range(repeat):
            for i in range(tx_count):
                while True:
                    from_node_idx = random.randint(0, len(self.nodes) - 1)
                    from_node = self.nodes[from_node_idx]
                    if from_node is not None:
                        break
                while True:
                    to_node_idx = random.randint(0, len(self.nodes) - 1)
                    to_node = self.nodes[to_node_idx]
                    if to_node is not None and from_node is not to_node:
                        break
                to_address = to_node.getnewaddress()
                txid = from_node.instantsendtoaddress(to_address, 0.01)
                self.wait_for_instant_lock(to_node, to_node_idx, txid)
            self.nodes[0].generate(6)
            self.sync_all()

    def wait_for_instant_lock(self, node, node_idx, txid, timeout=10):
        st = time.time()
        while time.time() < st + timeout:
            try:
                tx = node.gettransaction(txid)
            except:
                continue
            if tx is None:
                continue
            if tx['instantlock']:
                return
        raise AssertionError("wait_for_instant_lock timed out for: {} on node {}".format(txid, node_idx))

    def wait_for_winners(self, node, height, timeout=5):
        st = time.time()
        while time.time() < st + timeout:
            winners = node.masternode('winners')
            if str(height) in winners:
                if re.match('[0-9a-zA-Z]*:10', winners[str(height)]):
                    return winners
            time.sleep(0.5)
        raise AssertionError("wait_for_winners for height {} timed out: {}".format(height, node.masternode('winners')))

    def wait_for_mnlists(self, mns, include_legacy, include_protx, timeout=30, check=False):
        for node in self.nodes:
            self.wait_for_mnlist(node, mns, include_legacy, include_protx, timeout, check=check)

    def wait_for_mnlist(self, node, mns, include_legacy, include_protx, timeout=30, check=False):
        st = time.time()
        while time.time() < st + timeout:
            if check:
                node.masternode('check')
            if self.compare_mnlist(node, mns, include_legacy, include_protx):
                return
            time.sleep(0.5)
        raise AssertionError("wait_for_mnlist timed out")

    def assert_mnlists(self, mns, include_legacy, include_protx):
        for node in self.nodes:
            self.assert_mnlist(node, mns, include_legacy, include_protx)

    def assert_mnlist(self, node, mns, include_legacy, include_protx):
        if not self.compare_mnlist(node, mns, include_legacy, include_protx):
            expected = []
            for mn in mns:
                if (mn.is_protx and include_protx) or (not mn.is_protx and include_legacy):
                    expected.append('%s-%d' % (mn.collateral_txid, mn.collateral_vout))
            print('mnlist: ' + str(node.masternode('list', 'status')))
            print('expected: ' + str(expected))
            raise AssertionError("mnlists does not match provided mns")

    def wait_for_sporks(self, timeout=30):
        st = time.time()
        while time.time() < st + timeout:
            if self.compare_sporks():
                return
        raise AssertionError("wait_for_sporks timed out")

    def compare_sporks(self):
        sporks = self.nodes[0].spork('show')
        for node in self.nodes[1:]:
            sporks2 = node.spork('show')
            if sporks != sporks2:
                return False
        return True

    def compare_mnlist(self, node, mns, include_legacy, include_protx):
        mnlist = node.masternode('list', 'status')
        for mn in mns:
            s = '%s-%d' % (mn.collateral_txid, mn.collateral_vout)
            in_list = s in mnlist

            if mn.is_protx:
                if include_protx:
                    if not in_list:
                        return False
                else:
                    if in_list:
                        return False
            else:
                if include_legacy:
                    if not in_list:
                        return False
                else:
                    if in_list:
                        return False
            mnlist.pop(s, None)
        if len(mnlist) != 0:
            return False
        return True

    def wait_for_mnlists_same(self, timeout=30):
        st = time.time()
        while time.time() < st + timeout:
            mnlist = self.nodes[0].masternode('list', 'status')
            all_match = True
            for node in self.nodes[1:]:
                mnlist2 = node.masternode('list', 'status')
                if mnlist != mnlist2:
                    all_match = False
                    break
            if all_match:
                return
            time.sleep(0.5)
        raise AssertionError("wait_for_mnlists_same timed out")

    def test_fail_create_protx(self, node):
        # Try to create ProTx (should still fail)
        address = node.getnewaddress()
        key = node.getnewaddress()
        assert_raises_jsonrpc(None, "bad-tx-type", node.protx, 'register', address, '1000', '127.0.0.1:10000', '0', key, key, key, 0, address)

    def test_success_create_protx(self, node):
        address = node.getnewaddress()
        key = node.getnewaddress()
        txid = node.protx('register', address, '1000', '127.0.0.1:10000', '0', key, key, key, 0, address)
        rawtx = node.getrawtransaction(txid, 1)
        self.mine_double_spend(node, rawtx['vin'], address, use_mnmerkleroot_from_tip=True)
        self.sync_all()

    def spend_input(self, txid, vout, amount, with_dummy_input_output=False):
        # with_dummy_input_output is useful if you want to test reorgs with double spends of the TX without touching the actual txid/vout
        address = self.nodes[0].getnewaddress()
        target = {address: amount}
        if with_dummy_input_output:
            dummyaddress = self.nodes[0].getnewaddress()
            target[dummyaddress] = 1
        rawtx = self.nodes[0].createrawtransaction([{'txid': txid, 'vout': vout}], target)
        rawtx = self.nodes[0].fundrawtransaction(rawtx)['hex']
        rawtx = self.nodes[0].signrawtransaction(rawtx)['hex']
        new_txid = self.nodes[0].sendrawtransaction(rawtx)

        if with_dummy_input_output:
            decoded = self.nodes[0].decoderawtransaction(rawtx)
            for i in range(len(decoded['vout'])):
                # make sure this one can only be spent when explicitely creating a rawtx with these outputs as inputs
                # this ensures that no other TX is chaining on top of this TX
                lock = self.nodes[0].lockunspent(False, [{'txid': new_txid, 'vout': i}])
            for txin in decoded['vin']:
                if txin['txid'] != txid or txin['vout'] != vout:
                    return txin
        return None

    def mine_block(self, node, vtx=[], miner_address=None, mn_payee=None, mn_amount=None, use_mnmerkleroot_from_tip=False, expected_error=None):
        bt = node.getblocktemplate()
        height = bt['height']
        tip_hash = bt['previousblockhash']

        tip_block = node.getblock(tip_hash)

        coinbasevalue = bt['coinbasevalue']
        if miner_address is None:
            miner_address = node.getnewaddress()
        if mn_payee is None:
            if isinstance(bt['masternode'], list):
                mn_payee = bt['masternode'][0]['payee']
            else:
                mn_payee = bt['masternode']['payee']
        # we can't take the masternode payee amount from the template here as we might have additional fees in vtx

        # calculate fees that the block template included (we'll have to remove it from the coinbase as we won't
        # include the template's transactions
        bt_fees = 0
        for tx in bt['transactions']:
            bt_fees += tx['fee']

        new_fees = 0
        for tx in vtx:
            in_value = 0
            out_value = 0
            for txin in tx.vin:
                txout = node.gettxout("%064x" % txin.prevout.hash, txin.prevout.n, False)
                in_value += int(txout['value'] * COIN)
            for txout in tx.vout:
                out_value += txout.nValue
            new_fees += in_value - out_value

        # fix fees
        coinbasevalue -= bt_fees
        coinbasevalue += new_fees

        if mn_amount is None:
            mn_amount = get_masternode_payment(height, coinbasevalue)
        miner_amount = coinbasevalue - mn_amount

        outputs = {miner_address: str(Decimal(miner_amount) / COIN)}
        if mn_amount > 0:
            outputs[mn_payee] = str(Decimal(mn_amount) / COIN)

        coinbase = FromHex(CTransaction(), node.createrawtransaction([], outputs))
        coinbase.vin = create_coinbase(height).vin

        # We can't really use this one as it would result in invalid merkle roots for masternode lists
        if len(bt['coinbase_payload']) != 0:
            cbtx = FromHex(CCbTx(), bt['coinbase_payload'])
            if use_mnmerkleroot_from_tip:
                if 'cbTx' in tip_block:
                    cbtx.merkleRootMNList = int(tip_block['cbTx']['merkleRootMNList'], 16)
                else:
                    cbtx.merkleRootMNList = 0
            coinbase.nVersion = 3
            coinbase.nType = 5 # CbTx
            coinbase.extraPayload = cbtx.serialize()

        coinbase.calc_sha256()

        block = create_block(int(tip_hash, 16), coinbase)
        block.vtx += vtx
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()
        result = node.submitblock(ToHex(block))
        if expected_error is not None and result != expected_error:
            raise AssertionError('mining the block should have failed with error %s, but submitblock returned %s' % (expected_error, result))
        elif expected_error is None and result is not None:
            raise AssertionError('submitblock returned %s' % (result))

    def mine_double_spend(self, node, txins, target_address, use_mnmerkleroot_from_tip=False):
        amount = Decimal(0)
        for txin in txins:
            txout = node.gettxout(txin['txid'], txin['vout'], False)
            amount += txout['value']
        amount -= Decimal("0.001") # fee

        rawtx = node.createrawtransaction(txins, {target_address: amount})
        rawtx = node.signrawtransaction(rawtx)['hex']
        tx = FromHex(CTransaction(), rawtx)

        self.mine_block(node, [tx], use_mnmerkleroot_from_tip=use_mnmerkleroot_from_tip)

    def test_invalid_mn_payment(self, node):
        mn_payee = self.nodes[0].getnewaddress()
        self.mine_block(node, mn_payee=mn_payee, expected_error='bad-cb-payee')
        self.mine_block(node, mn_amount=1, expected_error='bad-cb-payee')

if __name__ == '__main__':
    DIP3Test().main()
