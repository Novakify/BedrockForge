#!/usr/bin/env python3
"""Explicit opt-in Vineflower launcher. No automatic download, no mod execution."""
import argparse
import pathlib
import subprocess
import sys

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--vineflower",required=True,help="Local Vineflower executable jar")
    p.add_argument("--jar",required=True,help="Java mod JAR to analyze")
    p.add_argument("--out",required=True,help="Empty destination for decompiled Java")
    p.add_argument("--timeout",type=int,default=900)
    p.add_argument("--run",action="store_true",help="Required: explicitly permit decompiler execution")
    args=p.parse_args()
    vineflower=pathlib.Path(args.vineflower).resolve()
    source=pathlib.Path(args.jar).resolve()
    destination=pathlib.Path(args.out).resolve()
    if not vineflower.is_file() or not source.is_file(): p.error("Source JAR or Vineflower JAR is missing")
    if args.timeout<1 or args.timeout>3600: p.error("Timeout must be between 1 and 3600 seconds")
    if destination.exists() and any(destination.iterdir()): p.error("Destination must be empty to avoid overwrites")
    command=["java","-jar",str(vineflower),str(source),str(destination)]
    print("Vineflower input:",source)
    print("Output:",destination)
    if not args.run:
        print("Dry run only: pass --run to execute; never commit third-party decompiled source.")
        return
    destination.mkdir(parents=True,exist_ok=True)
    completed=subprocess.run(command,check=False,timeout=args.timeout)
    raise SystemExit(completed.returncode)

if __name__=="__main__":main()
