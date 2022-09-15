#!/usr/bin/env python3

from argparse import ArgumentParser
import re
import subprocess

def default_iface(ip="8.8.8.8"):
    ip_r = subprocess.run(["ip", "route", "get", ip], capture_output=True, check=True)
    match = re.search("dev (.+) src", ip_r.stdout.decode("utf-8"))
    return match.group(1)

def iptables_ipv4_forward():
    subprocess.run(f"""
    sysctl net.ipv4.ip_forward=1
    """, shell=True)

def iptables_nat_forward(iface_in, iface_out):
    subprocess.run(f"""
    iptables -t nat -C POSTROUTING -o {iface_out} -j MASQUERADE 2>/dev/null || \
    iptables -t nat -A POSTROUTING -o {iface_out} -j MASQUERADE
    """, check=True, shell=True)

    subprocess.run(f"""
    iptables -C FORWARD -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT \
             2>/dev/null || \
    iptables -A FORWARD -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
    """, check=True, shell=True)

    subprocess.run(f"""
    iptables -C FORWARD -i {iface_in} -o {iface_out} -j ACCEPT 2>/dev/null || \
    iptables -A FORWARD -i {iface_in} -o {iface_out} -j ACCEPT
    """, check=True, shell=True)


if __name__ == "__main__":
    parser = ArgumentParser()
    parser.add_argument("int", help="Internal interface (usually TAP / PPP)")
    parser.add_argument("-o", "--out",
                        help="Interface with access to internet (usually ETH)")
    args = parser.parse_args()

    iface_out = args.out
    if not iface_out:
        iface_out = default_iface()
        print(f"Using default interface: {iface_out}")

    iptables_ipv4_forward()
    iptables_nat_forward(args.int, iface_out)
