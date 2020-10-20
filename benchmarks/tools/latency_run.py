#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2019, Intel Corporation

"""
latency_run.py - tool for running latency benchmarks and plotting them
with use of latency_plot.py.

Benchmark configurations are stored in benchconfig.py file as separate
dictionaries with names starting with 'bench_'. Configuration key 'testdir'
is a benchmark test directory. All other keys and their values are directly
translated into 'bench_simul' command line arguments.
"""

import sys
import os
import argparse
import subprocess as sp
import latency_plot as lp


try:
    import benchconfig
except ImportError:
    sys.exit('no benchconfig.py file provided')


class Benchmark():
    """Benchmark management"""
    def __init__(self, bin_, cfg):
        self.bin = bin_
        self.cmd = ''
        self.handle_special_keys(cfg)
        for k, v in cfg.items():
            self.cmd = self.cmd + ' {}={}'.format(k, v)
        if 'latency_samples' not in cfg:
            self.cmd = self.cmd + ' latency_samples=101'

    def handle_special_keys(self, cfg):
        """
        Handle configuration keys that are NOT a direct
        representations of bench_simul command line arguments
        """
        try:
            self.title = cfg.pop('title')
            cfg['latency_file'] = '{}.log'.format(self.title)
            self.out = cfg['latency_file']
            self.cmd = '{} {}'.format(self.bin, cfg.pop('testdir'))
            if 'numa_node' in cfg:
                self.cmd = 'numactl -N {} {}'.format(cfg.pop('numa_node'), self.cmd)
        except KeyError as e:
            sys.exit('No "{}" key provided to configuration'.format(e.args[0]))


    def add_to_plot(self):
        """Add benchmark to plot"""
        lp.add_data(self.out, self.title)

    def run(self, verbose):
        """Run benchmark"""
        if verbose:
            print(self.cmd)

        proc = sp.run(self.cmd.split(' '), universal_newlines=True,
                      stdout=sp.PIPE, stderr=sp.STDOUT)
        if proc.returncode != 0:
            sys.exit('benchmark failed: {}{}{}'.format(self.cmd, os.linesep,
                                                       proc.stdout))
        if verbose:
            print('{}{}{}'.format(self.cmd, os.linesep, proc.stdout))


def parse_config():
    """Read configurations from benchconfig.py file"""
    cfgs = []
    for k, v in vars(benchconfig).items():
        if k.startswith('bench_') and isinstance(v, dict):
            v['title'] = k.split('bench_')[1]
            cfgs.append(v)

    if not cfgs:
        sys.exit('No configs found in benchconfig.py - all configs should '
                 'be dictionaries starting with "bench_"')
    return cfgs


def parse_args():
    """Parse command line arguments"""
    parser = argparse.ArgumentParser()
    parser.add_argument('--bin', '-b', help='path to bench_simul binary',
                        required=True)
    parser.add_argument('--config', '-c', nargs='*', help="run only selected "
                        "configs from benchconfig (provide name without "
                        "'bench' at the beginning)")
    parser.add_argument('--yscale', '-y', help='Y-axis scale',
                        default='linear')
    parser.add_argument('--verbose', '-v', help='Print bench_simul output',
                        action='store_true')
    args = parser.parse_args()
    return args


def main():
    args = parse_args()
    file_cfgs = parse_config()
    if args.config:
        cfgs = [c for c in file_cfgs if c['title'] in args.config]
        if len(args.config) != len(cfgs):
            titles = os.linesep.join([c['title'] for c in file_cfgs])
            sys.exit('Invalid configuration provided, configurations defined'
                     ' in benchconfig.py:{}{}'.format(os.linesep, titles))
    else:
        cfgs = file_cfgs

    for c in cfgs:
        b = Benchmark(args.bin, c)
        b.run(args.verbose)
        b.add_to_plot()

    lp.draw_plot(args.yscale)


if __name__ == '__main__':
    main()
