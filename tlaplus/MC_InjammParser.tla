---------------------------- MODULE MC_InjammParser ---------------------------
(***************************************************************************)
(* TLC 起動用ラッパ。MaxLen を具体値に束縛する。                              *)
(* 実行: java -cp tla2tools.jar tlc2.TLC -continue MC_InjammParser            *)
(***************************************************************************)
EXTENDS TLC

VARIABLES tin, ct, vm

I == INSTANCE InjammParser WITH MaxLen <- 5, tin <- tin, ct <- ct, vm <- vm

Init == I!Init
Next == I!Next

TypeOK           == I!TypeOK
InvAgreement     == I!InvAgreement
InvRefVM         == I!InvRefVM
InvCTIllFormed   == I!InvCTIllFormed
InvRefCT_NoFalse == I!InvRefCT_NoFalse
=============================================================================
