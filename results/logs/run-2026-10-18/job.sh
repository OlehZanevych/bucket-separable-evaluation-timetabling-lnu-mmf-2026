#!/bin/bash
# one job: kind args...; output dir j_<name>, marked done when complete
cd $HOME/a2b/R
C=$HOME/mnt/Dissertation/Articles/a2-separable-search/code
kind=$1; shift
case $kind in
  gen) e=$1; s=$2; out=j_gen_${e}_$s
       case $e in permute|aiming) b="--budget 200000";; scaling) b="--budget 300000";; *) b="";; esac
       cmd="./exp $e --seeds $s --seedfrom $s $b";;
  ctt) i=$1; a=$2; s=$3; out=j_ctt_${i}_${a}_$s
       cmd="./exp ctt --seeds $s --seedfrom $s --budget 1000000 --arm $a --data d$i";;
  cost) out=j_cttcost; cmd="./exp ctt-cost --budget 400000 --data $C/data-ctt";;
  diag) i=$1; out=j_diag_$i; cmd="./exp_diag ctt --seeds 1 --budget 50000 --data d$i";;
esac
[ -f $out/done ] && exit 0
rm -rf $out; mkdir -p $out
$cmd --results $out > $out/log 2>&1 && touch $out/done
