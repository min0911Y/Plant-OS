;;; nasmtok.el --- lists of NASM assembler tokens
;;;
;;; This file contains list of tokens from the NASM x86
;;; assembler, automatically extracted from NASM 2.17rc0.
;;;
;;; This file is intended to be (require)d from a `nasm-mode'
;;; major mode definition.

(defconst nasm-decorator
  '("1to16" "1to2" "1to32" "1to4" "1to8" "rd-sae" "rn-sae" "ru-sae" "rz-sae"
    "sae" "z")
  "NASM 2.17rc0 decorator tokens for `nasm-mode'.")

(defconst nasm-directive
  '("absolute" "bits" "common" "cpu" "debug" "default" "export" "extern"
    "float" "global" "gprefix" "group" "gsuffix" "import" "library" "limit"
    "list" "lprefix" "lsuffix" "map" "maxdump" "module" "no_dead_strip"
    "nodepend" "noseclabels" "options" "org" "osabi" "pragma" "prefix"
    "required" "safeseh" "sectalign" "section" "segment" "static"
    "subsections_via_symbols" "suffix" "uppercase" "warning")
  "NASM 2.17rc0 directive tokens for `nasm-mode'.")

(defconst nasm-function
  '("__?bfloat16?__" "__?float128h?__" "__?float128l?__" "__?float16?__"
    "__?float32?__" "__?float64?__" "__?float80e?__" "__?float80m?__"
    "__?float8?__" "__?ilog2c?__" "__?ilog2e?__" "__?ilog2f?__"
    "__?ilog2w?__" "__?infinity?__" "__?nan?__" "__?qnan?__" "__?snan?__"
    "__?utf16?__" "__?utf16be?__" "__?utf16le?__" "__?utf32?__"
    "__?utf32be?__" "__?utf32le?__" "bfloat16" "float128h" "float128l"
    "float16" "float32" "float64" "float8" "float80e" "float80m" "ilog2c"
    "ilog2e" "ilog2f" "ilog2w" "infinity" "nan" "qnan" "snan" "utf16"
    "utf16be" "utf16le" "utf32" "utf32be" "utf32le")
  "NASM 2.17rc0 function tokens for `nasm-mode'.")

(defconst nasm-instruction
  '("aaa" "aad" "aadd" "aam" "aand" "aas" "adc" "adcx" "add" "addpd" "addps"
    "addsd" "addss" "addsubpd" "addsubps" "adox" "aesdec" "aesdeclast"
    "aesenc" "aesenclast" "aesimc" "aeskeygenassist" "and" "andn" "andnpd"
    "andnps" "andpd" "andps" "arpl" "axor" "bb0_reset" "bb1_reset" "bextr"
    "blcfill" "blci" "blcic" "blcmsk" "blcs" "blendpd" "blendps" "blendvpd"
    "blendvps" "blsfill" "blsi" "blsic" "blsmsk" "blsr" "bndcl" "bndcn"
    "bndcu" "bndldx" "bndmk" "bndmov" "bndstx" "bound" "bsf" "bsr" "bswap"
    "bt" "btc" "btr" "bts" "bzhi" "call" "cbw" "cdq" "cdqe" "clac" "clc"
    "cld" "cldemote" "clflush" "clflushopt" "clgi" "cli" "clrssbsy" "clts"
    "clui" "clwb" "clzero" "cmc" "cmova" "cmovae" "cmovb" "cmovbe" "cmovc"
    "cmove" "cmovg" "cmovge" "cmovl" "cmovle" "cmovna" "cmovnae" "cmovnb"
    "cmovnbe" "cmovnc" "cmovne" "cmovng" "cmovnge" "cmovnl" "cmovnle"
    "cmovno" "cmovnp" "cmovns" "cmovnz" "cmovo" "cmovp" "cmovpe" "cmovpo"
    "cmovs" "cmovz" "cmp" "cmpaexadd" "cmpaxadd" "cmpbexadd" "cmpbxadd"
    "cmpcxadd" "cmpeqpd" "cmpeqps" "cmpeqsd" "cmpeqss" "cmpexadd" "cmpgexadd"
    "cmpgxadd" "cmplepd" "cmpleps" "cmplesd" "cmpless" "cmplexadd" "cmpltpd"
    "cmpltps" "cmpltsd" "cmpltss" "cmplxadd" "cmpnaexadd" "cmpnaxadd"
    "cmpnbexadd" "cmpnbxadd" "cmpncxadd" "cmpneqpd" "cmpneqps" "cmpneqsd"
    "cmpneqss" "cmpnexadd" "cmpngexadd" "cmpngxadd" "cmpnlepd" "cmpnleps"
    "cmpnlesd" "cmpnless" "cmpnlexadd" "cmpnltpd" "cmpnltps" "cmpnltsd"
    "cmpnltss" "cmpnlxadd" "cmpnoxadd" "cmpnpxadd" "cmpnsxadd" "cmpnzxadd"
    "cmpordpd" "cmpordps" "cmpordsd" "cmpordss" "cmpoxadd" "cmppd"
    "cmppexadd" "cmppoxadd" "cmpps" "cmppxadd" "cmpsb" "cmpsd" "cmpsq"
    "cmpss" "cmpsw" "cmpsxadd" "cmpunordpd" "cmpunordps" "cmpunordsd"
    "cmpunordss" "cmpxchg" "cmpxchg16b" "cmpxchg486" "cmpxchg8b" "cmpzxadd"
    "comisd" "comiss" "cpu_read" "cpu_write" "cpuid" "cqo" "crc32" "cvtdq2pd"
    "cvtdq2ps" "cvtpd2dq" "cvtpd2pi" "cvtpd2ps" "cvtpi2pd" "cvtpi2ps"
    "cvtps2dq" "cvtps2pd" "cvtps2pi" "cvtsd2si" "cvtsd2ss" "cvtsi2sd"
    "cvtsi2ss" "cvtss2sd" "cvtss2si" "cvttpd2dq" "cvttpd2pi" "cvttps2dq"
    "cvttps2pi" "cvttsd2si" "cvttss2si" "cwd" "cwde" "daa" "das" "db" "dd"
    "dec" "div" "divpd" "divps" "divsd" "divss" "dmint" "do" "dppd" "dpps"
    "dq" "dt" "dw" "dy" "dz" "emms" "encls" "enclu" "enclv" "endbr32"
    "endbr64" "enqcmd" "enqcmds" "enter" "equ" "extractps" "extrq" "f2xm1"
    "fabs" "fadd" "faddp" "fbld" "fbstp" "fchs" "fclex" "fcmovb" "fcmovbe"
    "fcmove" "fcmovnb" "fcmovnbe" "fcmovne" "fcmovnu" "fcmovu" "fcom" "fcomi"
    "fcomip" "fcomp" "fcompp" "fcos" "fdecstp" "fdisi" "fdiv" "fdivp" "fdivr"
    "fdivrp" "femms" "feni" "ffree" "ffreep" "fiadd" "ficom" "ficomp" "fidiv"
    "fidivr" "fild" "fimul" "fincstp" "finit" "fist" "fistp" "fisttp" "fisub"
    "fisubr" "fld" "fld1" "fldcw" "fldenv" "fldl2e" "fldl2t" "fldlg2"
    "fldln2" "fldpi" "fldz" "fmul" "fmulp" "fnclex" "fndisi" "fneni" "fninit"
    "fnop" "fnsave" "fnstcw" "fnstenv" "fnstsw" "fpatan" "fprem" "fprem1"
    "fptan" "frndint" "frstor" "fsave" "fscale" "fsetpm" "fsin" "fsincos"
    "fsqrt" "fst" "fstcw" "fstenv" "fstp" "fstsw" "fsub" "fsubp" "fsubr"
    "fsubrp" "ftst" "fucom" "fucomi" "fucomip" "fucomp" "fucompp" "fwait"
    "fxam" "fxch" "fxrstor" "fxrstor64" "fxsave" "fxsave64" "fxtract" "fyl2x"
    "fyl2xp1" "getsec" "gf2p8affineinvqb" "gf2p8affineqb" "gf2p8mulb"
    "haddpd" "haddps" "hint_nop0" "hint_nop1" "hint_nop10" "hint_nop11"
    "hint_nop12" "hint_nop13" "hint_nop14" "hint_nop15" "hint_nop16"
    "hint_nop17" "hint_nop18" "hint_nop19" "hint_nop2" "hint_nop20"
    "hint_nop21" "hint_nop22" "hint_nop23" "hint_nop24" "hint_nop25"
    "hint_nop26" "hint_nop27" "hint_nop28" "hint_nop29" "hint_nop3"
    "hint_nop30" "hint_nop31" "hint_nop32" "hint_nop33" "hint_nop34"
    "hint_nop35" "hint_nop36" "hint_nop37" "hint_nop38" "hint_nop39"
    "hint_nop4" "hint_nop40" "hint_nop41" "hint_nop42" "hint_nop43"
    "hint_nop44" "hint_nop45" "hint_nop46" "hint_nop47" "hint_nop48"
    "hint_nop49" "hint_nop5" "hint_nop50" "hint_nop51" "hint_nop52"
    "hint_nop53" "hint_nop54" "hint_nop55" "hint_nop56" "hint_nop57"
    "hint_nop58" "hint_nop59" "hint_nop6" "hint_nop60" "hint_nop61"
    "hint_nop62" "hint_nop63" "hint_nop7" "hint_nop8" "hint_nop9" "hlt"
    "hreset" "hsubpd" "hsubps" "ibts" "icebp" "idiv" "imul" "in" "inc"
    "incbin" "incsspd" "incsspq" "insb" "insd" "insertps" "insertq" "insw"
    "int" "int01" "int03" "int1" "int3" "into" "invd" "invept" "invlpg"
    "invlpga" "invpcid" "invvpid" "iret" "iretd" "iretq" "iretw" "ja" "jae"
    "jb" "jbe" "jc" "jcxz" "je" "jecxz" "jg" "jge" "jl" "jle" "jmp" "jmpe"
    "jna" "jnae" "jnb" "jnbe" "jnc" "jne" "jng" "jnge" "jnl" "jnle" "jno"
    "jnp" "jns" "jnz" "jo" "jp" "jpe" "jpo" "jrcxz" "js" "jz" "kadd" "kaddb"
    "kaddd" "kaddq" "kaddw" "kand" "kandb" "kandd" "kandn" "kandnb" "kandnd"
    "kandnq" "kandnw" "kandq" "kandw" "kmov" "kmovb" "kmovd" "kmovq" "kmovw"
    "knot" "knotb" "knotd" "knotq" "knotw" "kor" "korb" "kord" "korq"
    "kortest" "kortestb" "kortestd" "kortestq" "kortestw" "korw" "kshiftl"
    "kshiftlb" "kshiftld" "kshiftlq" "kshiftlw" "kshiftr" "kshiftrb"
    "kshiftrd" "kshiftrq" "kshiftrw" "ktest" "ktestb" "ktestd" "ktestq"
    "ktestw" "kunpck" "kunpckbw" "kunpckdq" "kunpckwd" "kxnor" "kxnorb"
    "kxnord" "kxnorq" "kxnorw" "kxor" "kxorb" "kxord" "kxorq" "kxorw" "lahf"
    "lar" "lddqu" "ldmxcsr" "lds" "ldtilecfg" "lea" "leave" "les" "lfence"
    "lfs" "lgdt" "lgs" "lidt" "lldt" "llwpcb" "lmsw" "loadall" "loadall286"
    "lodsb" "lodsd" "lodsq" "lodsw" "loop" "loope" "loopne" "loopnz" "loopz"
    "lsl" "lss" "ltr" "lwpins" "lwpval" "lzcnt" "maskmovdqu" "maskmovq"
    "maxpd" "maxps" "maxsd" "maxss" "mfence" "minpd" "minps" "minsd" "minss"
    "monitor" "monitorx" "montmul" "mov" "movapd" "movaps" "movbe" "movd"
    "movddup" "movdir64b" "movdiri" "movdq2q" "movdqa" "movdqu" "movhlps"
    "movhpd" "movhps" "movlhps" "movlpd" "movlps" "movmskpd" "movmskps"
    "movntdq" "movntdqa" "movnti" "movntpd" "movntps" "movntq" "movntsd"
    "movntss" "movq" "movq2dq" "movsb" "movsd" "movshdup" "movsldup" "movsq"
    "movss" "movsw" "movsx" "movsxd" "movupd" "movups" "movzx" "mpsadbw"
    "mul" "mulpd" "mulps" "mulsd" "mulss" "mulx" "mwait" "mwaitx" "neg" "nop"
    "not" "or" "orpd" "orps" "out" "outsb" "outsd" "outsw" "pabsb" "pabsd"
    "pabsw" "packssdw" "packsswb" "packusdw" "packuswb" "paddb" "paddd"
    "paddq" "paddsb" "paddsiw" "paddsw" "paddusb" "paddusw" "paddw" "palignr"
    "pand" "pandn" "pause" "paveb" "pavgb" "pavgusb" "pavgw" "pblendvb"
    "pblendw" "pclmulhqhqdq" "pclmulhqlqdq" "pclmullqhqdq" "pclmullqlqdq"
    "pclmulqdq" "pcmpeqb" "pcmpeqd" "pcmpeqq" "pcmpeqw" "pcmpestri"
    "pcmpestrm" "pcmpgtb" "pcmpgtd" "pcmpgtq" "pcmpgtw" "pcmpistri"
    "pcmpistrm" "pcommit" "pconfig" "pdep" "pdistib" "pext" "pextrb" "pextrd"
    "pextrq" "pextrw" "pf2id" "pf2iw" "pfacc" "pfadd" "pfcmpeq" "pfcmpge"
    "pfcmpgt" "pfmax" "pfmin" "pfmul" "pfnacc" "pfpnacc" "pfrcp" "pfrcpit1"
    "pfrcpit2" "pfrcpv" "pfrsqit1" "pfrsqrt" "pfrsqrtv" "pfsub" "pfsubr"
    "phaddd" "phaddsw" "phaddw" "phminposuw" "phsubd" "phsubsw" "phsubw"
    "pi2fd" "pi2fw" "pinsrb" "pinsrd" "pinsrq" "pinsrw" "pmachriw"
    "pmaddubsw" "pmaddwd" "pmagw" "pmaxsb" "pmaxsd" "pmaxsw" "pmaxub"
    "pmaxud" "pmaxuw" "pminsb" "pminsd" "pminsw" "pminub" "pminud" "pminuw"
    "pmovmskb" "pmovsxbd" "pmovsxbq" "pmovsxbw" "pmovsxdq" "pmovsxwd"
    "pmovsxwq" "pmovzxbd" "pmovzxbq" "pmovzxbw" "pmovzxdq" "pmovzxwd"
    "pmovzxwq" "pmuldq" "pmulhriw" "pmulhrsw" "pmulhrwa" "pmulhrwc" "pmulhuw"
    "pmulhw" "pmulld" "pmullw" "pmuludq" "pmvgezb" "pmvlzb" "pmvnzb" "pmvzb"
    "pop" "popa" "popad" "popaw" "popcnt" "popf" "popfd" "popfq" "popfw"
    "por" "prefetch" "prefetchit0" "prefetchit1" "prefetchnta" "prefetcht0"
    "prefetcht1" "prefetcht2" "prefetchw" "prefetchwt1" "psadbw" "pshufb"
    "pshufd" "pshufhw" "pshuflw" "pshufw" "psignb" "psignd" "psignw" "pslld"
    "pslldq" "psllq" "psllw" "psrad" "psraw" "psrld" "psrldq" "psrlq" "psrlw"
    "psubb" "psubd" "psubq" "psubsb" "psubsiw" "psubsw" "psubusb" "psubusw"
    "psubw" "pswapd" "ptest" "ptwrite" "punpckhbw" "punpckhdq" "punpckhqdq"
    "punpckhwd" "punpcklbw" "punpckldq" "punpcklqdq" "punpcklwd" "push"
    "pusha" "pushad" "pushaw" "pushf" "pushfd" "pushfq" "pushfw" "pvalidate"
    "pxor" "rcl" "rcpps" "rcpss" "rcr" "rdfsbase" "rdgsbase" "rdm" "rdmsr"
    "rdmsrlist" "rdpid" "rdpkru" "rdpmc" "rdrand" "rdseed" "rdshr" "rdsspd"
    "rdsspq" "rdtsc" "rdtscp" "resb" "resd" "reso" "resq" "rest" "resw"
    "resy" "resz" "ret" "retd" "retf" "retfd" "retfq" "retfw" "retn" "retnd"
    "retnq" "retnw" "retq" "retw" "rmpadjust" "rol" "ror" "rorx" "roundpd"
    "roundps" "roundsd" "roundss" "rsdc" "rsldt" "rsm" "rsqrtps" "rsqrtss"
    "rstorssp" "rsts" "sahf" "sal" "salc" "sar" "sarx" "saveprevssp" "sbb"
    "scasb" "scasd" "scasq" "scasw" "senduipi" "serialize" "seta" "setae"
    "setb" "setbe" "setc" "sete" "setg" "setge" "setl" "setle" "setna"
    "setnae" "setnb" "setnbe" "setnc" "setne" "setng" "setnge" "setnl"
    "setnle" "setno" "setnp" "setns" "setnz" "seto" "setp" "setpe" "setpo"
    "sets" "setssbsy" "setz" "sfence" "sgdt" "sha1msg1" "sha1msg2"
    "sha1nexte" "sha1rnds4" "sha256msg1" "sha256msg2" "sha256rnds2" "shl"
    "shld" "shlx" "shr" "shrd" "shrx" "shufpd" "shufps" "sidt" "skinit"
    "sldt" "slwpcb" "smi" "smint" "smintold" "smsw" "sqrtpd" "sqrtps"
    "sqrtsd" "sqrtss" "stac" "stc" "std" "stgi" "sti" "stmxcsr" "stosb"
    "stosd" "stosq" "stosw" "str" "sttilecfg" "stui" "sub" "subpd" "subps"
    "subsd" "subss" "svdc" "svldt" "svts" "swapgs" "syscall" "sysenter"
    "sysexit" "sysret" "t1mskc" "tdpbf16ps" "tdpbssd" "tdpbsud" "tdpbusd"
    "tdpbuud" "test" "testui" "tileloadd" "tileloaddt1" "tilerelease"
    "tilestored" "tilezero" "tpause" "tzcnt" "tzmsk" "ucomisd" "ucomiss"
    "ud0" "ud1" "ud2" "ud2a" "ud2b" "uiret" "umonitor" "umov" "umwait"
    "unpckhpd" "unpckhps" "unpcklpd" "unpcklps" "v4dpwssd" "v4dpwssds"
    "v4fmaddps" "v4fmaddss" "v4fnmaddps" "v4fnmaddss" "vaddpd" "vaddph"
    "vaddps" "vaddsd" "vaddsh" "vaddss" "vaddsubpd" "vaddsubps" "vaesdec"
    "vaesdeclast" "vaesenc" "vaesenclast" "vaesimc" "vaeskeygenassist"
    "valignd" "valignq" "vandnpd" "vandnps" "vandpd" "vandps" "vbcstnebf16ps"
    "vbcstnesh2ps" "vblendmpd" "vblendmps" "vblendpd" "vblendps" "vblendvpd"
    "vblendvps" "vbroadcastf128" "vbroadcastf32x2" "vbroadcastf32x4"
    "vbroadcastf32x8" "vbroadcastf64x2" "vbroadcastf64x4" "vbroadcasti128"
    "vbroadcasti32x2" "vbroadcasti32x4" "vbroadcasti32x8" "vbroadcasti64x2"
    "vbroadcasti64x4" "vbroadcastsd" "vbroadcastss" "vcmpeq_oqpd"
    "vcmpeq_oqps" "vcmpeq_oqsd" "vcmpeq_oqss" "vcmpeq_ospd" "vcmpeq_osps"
    "vcmpeq_ossd" "vcmpeq_osss" "vcmpeq_uqpd" "vcmpeq_uqps" "vcmpeq_uqsd"
    "vcmpeq_uqss" "vcmpeq_uspd" "vcmpeq_usps" "vcmpeq_ussd" "vcmpeq_usss"
    "vcmpeqpd" "vcmpeqps" "vcmpeqsd" "vcmpeqss" "vcmpfalse_oqpd"
    "vcmpfalse_oqps" "vcmpfalse_oqsd" "vcmpfalse_oqss" "vcmpfalse_ospd"
    "vcmpfalse_osps" "vcmpfalse_ossd" "vcmpfalse_osss" "vcmpfalsepd"
    "vcmpfalseps" "vcmpfalsesd" "vcmpfalsess" "vcmpge_oqpd" "vcmpge_oqps"
    "vcmpge_oqsd" "vcmpge_oqss" "vcmpge_ospd" "vcmpge_osps" "vcmpge_ossd"
    "vcmpge_osss" "vcmpgepd" "vcmpgeps" "vcmpgesd" "vcmpgess" "vcmpgt_oqpd"
    "vcmpgt_oqps" "vcmpgt_oqsd" "vcmpgt_oqss" "vcmpgt_ospd" "vcmpgt_osps"
    "vcmpgt_ossd" "vcmpgt_osss" "vcmpgtpd" "vcmpgtps" "vcmpgtsd" "vcmpgtss"
    "vcmple_oqpd" "vcmple_oqps" "vcmple_oqsd" "vcmple_oqss" "vcmple_ospd"
    "vcmple_osps" "vcmple_ossd" "vcmple_osss" "vcmplepd" "vcmpleps"
    "vcmplesd" "vcmpless" "vcmplt_oqpd" "vcmplt_oqps" "vcmplt_oqsd"
    "vcmplt_oqss" "vcmplt_ospd" "vcmplt_osps" "vcmplt_ossd" "vcmplt_osss"
    "vcmpltpd" "vcmpltps" "vcmpltsd" "vcmpltss" "vcmpneq_oqpd" "vcmpneq_oqps"
    "vcmpneq_oqsd" "vcmpneq_oqss" "vcmpneq_ospd" "vcmpneq_osps"
    "vcmpneq_ossd" "vcmpneq_osss" "vcmpneq_uqpd" "vcmpneq_uqps"
    "vcmpneq_uqsd" "vcmpneq_uqss" "vcmpneq_uspd" "vcmpneq_usps"
    "vcmpneq_ussd" "vcmpneq_usss" "vcmpneqpd" "vcmpneqps" "vcmpneqsd"
    "vcmpneqss" "vcmpnge_uqpd" "vcmpnge_uqps" "vcmpnge_uqsd" "vcmpnge_uqss"
    "vcmpnge_uspd" "vcmpnge_usps" "vcmpnge_ussd" "vcmpnge_usss" "vcmpngepd"
    "vcmpngeps" "vcmpngesd" "vcmpngess" "vcmpngt_uqpd" "vcmpngt_uqps"
    "vcmpngt_uqsd" "vcmpngt_uqss" "vcmpngt_uspd" "vcmpngt_usps"
    "vcmpngt_ussd" "vcmpngt_usss" "vcmpngtpd" "vcmpngtps" "vcmpngtsd"
    "vcmpngtss" "vcmpnle_uqpd" "vcmpnle_uqps" "vcmpnle_uqsd" "vcmpnle_uqss"
    "vcmpnle_uspd" "vcmpnle_usps" "vcmpnle_ussd" "vcmpnle_usss" "vcmpnlepd"
    "vcmpnleps" "vcmpnlesd" "vcmpnless" "vcmpnlt_uqpd" "vcmpnlt_uqps"
    "vcmpnlt_uqsd" "vcmpnlt_uqss" "vcmpnlt_uspd" "vcmpnlt_usps"
    "vcmpnlt_ussd" "vcmpnlt_usss" "vcmpnltpd" "vcmpnltps" "vcmpnltsd"
    "vcmpnltss" "vcmpord_qpd" "vcmpord_qps" "vcmpord_qsd" "vcmpord_qss"
    "vcmpord_spd" "vcmpord_sps" "vcmpord_ssd" "vcmpord_sss" "vcmpordpd"
    "vcmpordps" "vcmpordsd" "vcmpordss" "vcmppd" "vcmpph" "vcmpps" "vcmpsd"
    "vcmpsh" "vcmpss" "vcmptrue_uqpd" "vcmptrue_uqps" "vcmptrue_uqsd"
    "vcmptrue_uqss" "vcmptrue_uspd" "vcmptrue_usps" "vcmptrue_ussd"
    "vcmptrue_usss" "vcmptruepd" "vcmptrueps" "vcmptruesd" "vcmptruess"
    "vcmpunord_qpd" "vcmpunord_qps" "vcmpunord_qsd" "vcmpunord_qss"
    "vcmpunord_spd" "vcmpunord_sps" "vcmpunord_ssd" "vcmpunord_sss"
    "vcmpunordpd" "vcmpunordps" "vcmpunordsd" "vcmpunordss" "vcomisd"
    "vcomish" "vcomiss" "vcompresspd" "vcompressps" "vcvtdq2pd" "vcvtdq2ph"
    "vcvtdq2ps" "vcvtne2ps2bf16" "vcvtneebf162ps" "vcvtneeph2ps"
    "vcvtneobf162ps" "vcvtneoph2ps" "vcvtneps2bf16" "vcvtpd2dq" "vcvtpd2ph"
    "vcvtpd2ps" "vcvtpd2qq" "vcvtpd2udq" "vcvtpd2uqq" "vcvtph2dq" "vcvtph2pd"
    "vcvtph2ps" "vcvtph2psx" "vcvtph2qq" "vcvtph2udq" "vcvtph2uqq"
    "vcvtph2uw" "vcvtph2w" "vcvtps2dq" "vcvtps2pd" "vcvtps2ph" "vcvtps2qq"
    "vcvtps2udq" "vcvtps2uqq" "vcvtqq2pd" "vcvtqq2ph" "vcvtqq2ps" "vcvtsd2sh"
    "vcvtsd2si" "vcvtsd2ss" "vcvtsd2usi" "vcvtsh2sd" "vcvtsh2si" "vcvtsh2ss"
    "vcvtsh2usi" "vcvtsi2sd" "vcvtsi2sh" "vcvtsi2ss" "vcvtss2sd" "vcvtss2sh"
    "vcvtss2si" "vcvtss2usi" "vcvttpd2dq" "vcvttpd2qq" "vcvttpd2udq"
    "vcvttpd2uqq" "vcvttph2dq" "vcvttph2qq" "vcvttph2udq" "vcvttph2uqq"
    "vcvttph2uw" "vcvttph2w" "vcvttps2dq" "vcvttps2qq" "vcvttps2udq"
    "vcvttps2uqq" "vcvttsd2si" "vcvttsd2usi" "vcvttsh2si" "vcvttsh2usi"
    "vcvttss2si" "vcvttss2usi" "vcvtudq2pd" "vcvtudq2ph" "vcvtudq2ps"
    "vcvtuqq2pd" "vcvtuqq2ph" "vcvtuqq2ps" "vcvtusi2sd" "vcvtusi2sh"
    "vcvtusi2ss" "vcvtuw2ph" "vcvtw2ph" "vdbpsadbw" "vdivpd" "vdivph"
    "vdivps" "vdivsd" "vdivsh" "vdivss" "vdpbf16ps" "vdppd" "vdpps"
    "vendscaleph" "vendscalesh" "verr" "verw" "vexp2pd" "vexp2ps" "vexpandpd"
    "vexpandps" "vextractf128" "vextractf32x4" "vextractf32x8"
    "vextractf64x2" "vextractf64x4" "vextracti128" "vextracti32x4"
    "vextracti32x8" "vextracti64x2" "vextracti64x4" "vextractps" "vfcmaddcph"
    "vfcmaddcsh" "vfcmulcpch" "vfcmulcsh" "vfixupimmpd" "vfixupimmps"
    "vfixupimmsd" "vfixupimmss" "vfmadd123pd" "vfmadd123ps" "vfmadd123sd"
    "vfmadd123ss" "vfmadd132pd" "vfmadd132ph" "vfmadd132ps" "vfmadd132sd"
    "vfmadd132ss" "vfmadd213pd" "vfmadd213ph" "vfmadd213ps" "vfmadd213sd"
    "vfmadd213ss" "vfmadd231pd" "vfmadd231ph" "vfmadd231ps" "vfmadd231sd"
    "vfmadd231ss" "vfmadd312pd" "vfmadd312ps" "vfmadd312sd" "vfmadd312ss"
    "vfmadd321pd" "vfmadd321ps" "vfmadd321sd" "vfmadd321ss" "vfmaddcph"
    "vfmaddcsh" "vfmaddpd" "vfmaddps" "vfmaddsd" "vfmaddss" "vfmaddsub123pd"
    "vfmaddsub123ps" "vfmaddsub132pd" "vfmaddsub132ph" "vfmaddsub132ps"
    "vfmaddsub213pd" "vfmaddsub213ph" "vfmaddsub213ps" "vfmaddsub231pd"
    "vfmaddsub231ph" "vfmaddsub231ps" "vfmaddsub312pd" "vfmaddsub312ps"
    "vfmaddsub321pd" "vfmaddsub321ps" "vfmaddsubpd" "vfmaddsubps"
    "vfmsub123pd" "vfmsub123ps" "vfmsub123sd" "vfmsub123ss" "vfmsub132pd"
    "vfmsub132ph" "vfmsub132ps" "vfmsub132sd" "vfmsub132ss" "vfmsub213pd"
    "vfmsub213ph" "vfmsub213ps" "vfmsub213sd" "vfmsub213ss" "vfmsub231pd"
    "vfmsub231ph" "vfmsub231ps" "vfmsub231sd" "vfmsub231ss" "vfmsub312pd"
    "vfmsub312ps" "vfmsub312sd" "vfmsub312ss" "vfmsub321pd" "vfmsub321ps"
    "vfmsub321sd" "vfmsub321ss" "vfmsubadd123pd" "vfmsubadd123ps"
    "vfmsubadd132pd" "vfmsubadd132ph" "vfmsubadd132ps" "vfmsubadd213pd"
    "vfmsubadd213ph" "vfmsubadd213ps" "vfmsubadd231pd" "vfmsubadd231ph"
    "vfmsubadd231ps" "vfmsubadd312pd" "vfmsubadd312ps" "vfmsubadd321pd"
    "vfmsubadd321ps" "vfmsubaddpd" "vfmsubaddps" "vfmsubpd" "vfmsubps"
    "vfmsubsd" "vfmsubss" "vfmulcpch" "vfmulcsh" "vfnmadd123pd"
    "vfnmadd123ps" "vfnmadd123sd" "vfnmadd123ss" "vfnmadd132pd"
    "vfnmadd132ps" "vfnmadd132sd" "vfnmadd132ss" "vfnmadd213pd"
    "vfnmadd213ps" "vfnmadd213sd" "vfnmadd213ss" "vfnmadd231pd"
    "vfnmadd231ps" "vfnmadd231sd" "vfnmadd231ss" "vfnmadd312pd"
    "vfnmadd312ps" "vfnmadd312sd" "vfnmadd312ss" "vfnmadd321pd"
    "vfnmadd321ps" "vfnmadd321sd" "vfnmadd321ss" "vfnmaddpd" "vfnmaddps"
    "vfnmaddsd" "vfnmaddss" "vfnmsub123pd" "vfnmsub123ps" "vfnmsub123sd"
    "vfnmsub123ss" "vfnmsub132pd" "vfnmsub132ps" "vfnmsub132sd"
    "vfnmsub132ss" "vfnmsub213pd" "vfnmsub213ps" "vfnmsub213sd"
    "vfnmsub213ss" "vfnmsub231pd" "vfnmsub231ps" "vfnmsub231sd"
    "vfnmsub231ss" "vfnmsub312pd" "vfnmsub312ps" "vfnmsub312sd"
    "vfnmsub312ss" "vfnmsub321pd" "vfnmsub321ps" "vfnmsub321sd"
    "vfnmsub321ss" "vfnmsubpd" "vfnmsubps" "vfnmsubsd" "vfnmsubss"
    "vfpclasspd" "vfpclassph" "vfpclassps" "vfpclasssd" "vfpclasssh"
    "vfpclassss" "vfrczpd" "vfrczps" "vfrczsd" "vfrczss" "vgatherdpd"
    "vgatherdps" "vgatherpf0dpd" "vgatherpf0dps" "vgatherpf0qpd"
    "vgatherpf0qps" "vgatherpf1dpd" "vgatherpf1dps" "vgatherpf1qpd"
    "vgatherpf1qps" "vgatherqpd" "vgatherqps" "vgetexppd" "vgetexpph"
    "vgetexpps" "vgetexpsd" "vgetexpsh" "vgetexpss" "vgetmantpd" "vgetmantph"
    "vgetmantps" "vgetmantsd" "vgetmantsh" "vgetmantss" "vgetmaxph"
    "vgetmaxsh" "vgetminph" "vgetminsh" "vgf2p8affineinvqb" "vgf2p8affineqb"
    "vgf2p8mulb" "vhaddpd" "vhaddps" "vhsubpd" "vhsubps" "vinsertf128"
    "vinsertf32x4" "vinsertf32x8" "vinsertf64x2" "vinsertf64x4" "vinserti128"
    "vinserti32x4" "vinserti32x8" "vinserti64x2" "vinserti64x4" "vinsertps"
    "vlddqu" "vldmxcsr" "vldqqu" "vmaskmovdqu" "vmaskmovpd" "vmaskmovps"
    "vmaxpd" "vmaxps" "vmaxsd" "vmaxss" "vmcall" "vmclear" "vmfunc" "vmgexit"
    "vminpd" "vminps" "vminsd" "vminss" "vmlaunch" "vmload" "vmmcall"
    "vmovapd" "vmovaps" "vmovd" "vmovddup" "vmovdqa" "vmovdqa32" "vmovdqa64"
    "vmovdqu" "vmovdqu16" "vmovdqu32" "vmovdqu64" "vmovdqu8" "vmovhlps"
    "vmovhpd" "vmovhps" "vmovlhps" "vmovlpd" "vmovlps" "vmovmskpd"
    "vmovmskps" "vmovntdq" "vmovntdqa" "vmovntpd" "vmovntps" "vmovntqq"
    "vmovq" "vmovqqa" "vmovqqu" "vmovsd" "vmovsh" "vmovshdup" "vmovsldup"
    "vmovss" "vmovupd" "vmovups" "vmovw" "vmpsadbw" "vmptrld" "vmptrst"
    "vmread" "vmresume" "vmrun" "vmsave" "vmulpd" "vmulph" "vmulps" "vmulsd"
    "vmulsh" "vmulss" "vmwrite" "vmxoff" "vmxon" "vorpd" "vorps"
    "vp2intersectd" "vpabsb" "vpabsd" "vpabsq" "vpabsw" "vpackssdw"
    "vpacksswb" "vpackusdw" "vpackuswb" "vpaddb" "vpaddd" "vpaddq" "vpaddsb"
    "vpaddsw" "vpaddusb" "vpaddusw" "vpaddw" "vpalignr" "vpand" "vpandd"
    "vpandn" "vpandnd" "vpandnq" "vpandq" "vpavgb" "vpavgw" "vpblendd"
    "vpblendmb" "vpblendmd" "vpblendmq" "vpblendmw" "vpblendvb" "vpblendw"
    "vpbroadcastb" "vpbroadcastd" "vpbroadcastmb2q" "vpbroadcastmw2d"
    "vpbroadcastq" "vpbroadcastw" "vpclmulhqhqdq" "vpclmulhqlqdq"
    "vpclmullqhqdq" "vpclmullqlqdq" "vpclmulqdq" "vpcmov" "vpcmpb" "vpcmpd"
    "vpcmpeqb" "vpcmpeqd" "vpcmpeqq" "vpcmpequb" "vpcmpequd" "vpcmpequq"
    "vpcmpequw" "vpcmpeqw" "vpcmpestri" "vpcmpestrm" "vpcmpgeb" "vpcmpged"
    "vpcmpgeq" "vpcmpgeub" "vpcmpgeud" "vpcmpgeuq" "vpcmpgeuw" "vpcmpgew"
    "vpcmpgtb" "vpcmpgtd" "vpcmpgtq" "vpcmpgtub" "vpcmpgtud" "vpcmpgtuq"
    "vpcmpgtuw" "vpcmpgtw" "vpcmpistri" "vpcmpistrm" "vpcmpleb" "vpcmpled"
    "vpcmpleq" "vpcmpleub" "vpcmpleud" "vpcmpleuq" "vpcmpleuw" "vpcmplew"
    "vpcmpltb" "vpcmpltd" "vpcmpltq" "vpcmpltub" "vpcmpltud" "vpcmpltuq"
    "vpcmpltuw" "vpcmpltw" "vpcmpneqb" "vpcmpneqd" "vpcmpneqq" "vpcmpnequb"
    "vpcmpnequd" "vpcmpnequq" "vpcmpnequw" "vpcmpneqw" "vpcmpngtb"
    "vpcmpngtd" "vpcmpngtq" "vpcmpngtub" "vpcmpngtud" "vpcmpngtuq"
    "vpcmpngtuw" "vpcmpngtw" "vpcmpnleb" "vpcmpnled" "vpcmpnleq" "vpcmpnleub"
    "vpcmpnleud" "vpcmpnleuq" "vpcmpnleuw" "vpcmpnlew" "vpcmpnltb"
    "vpcmpnltd" "vpcmpnltq" "vpcmpnltub" "vpcmpnltud" "vpcmpnltuq"
    "vpcmpnltuw" "vpcmpnltw" "vpcmpq" "vpcmpub" "vpcmpud" "vpcmpuq" "vpcmpuw"
    "vpcmpw" "vpcomb" "vpcomd" "vpcompressb" "vpcompressd" "vpcompressq"
    "vpcompressw" "vpcomq" "vpcomub" "vpcomud" "vpcomuq" "vpcomuw" "vpcomw"
    "vpconflictd" "vpconflictq" "vpdpbssd" "vpdpbssds" "vpdpbsud" "vpdpbsuds"
    "vpdpbusd" "vpdpbusds" "vpdpbuud" "vpdpbuuds" "vpdpwssd" "vpdpwssds"
    "vperm2f128" "vperm2i128" "vpermb" "vpermd" "vpermi2b" "vpermi2d"
    "vpermi2pd" "vpermi2ps" "vpermi2q" "vpermi2w" "vpermilpd" "vpermilps"
    "vpermpd" "vpermps" "vpermq" "vpermt2b" "vpermt2d" "vpermt2pd"
    "vpermt2ps" "vpermt2q" "vpermt2w" "vpermw" "vpexpandb" "vpexpandd"
    "vpexpandq" "vpexpandw" "vpextrb" "vpextrd" "vpextrq" "vpextrw"
    "vpgatherdd" "vpgatherdq" "vpgatherqd" "vpgatherqq" "vphaddbd" "vphaddbq"
    "vphaddbw" "vphaddd" "vphadddq" "vphaddsw" "vphaddubd" "vphaddubq"
    "vphaddubw" "vphaddudq" "vphadduwd" "vphadduwq" "vphaddw" "vphaddwd"
    "vphaddwq" "vphminposuw" "vphsubbw" "vphsubd" "vphsubdq" "vphsubsw"
    "vphsubw" "vphsubwd" "vpinsrb" "vpinsrd" "vpinsrq" "vpinsrw" "vplzcntd"
    "vplzcntq" "vpmacsdd" "vpmacsdqh" "vpmacsdql" "vpmacssdd" "vpmacssdqh"
    "vpmacssdql" "vpmacsswd" "vpmacssww" "vpmacswd" "vpmacsww" "vpmadcsswd"
    "vpmadcswd" "vpmadd132ph" "vpmadd132sh" "vpmadd213ph" "vpmadd213sh"
    "vpmadd231ph" "vpmadd231sh" "vpmadd52huq" "vpmadd52luq" "vpmaddubsw"
    "vpmaddwd" "vpmaskmovd" "vpmaskmovq" "vpmaxsb" "vpmaxsd" "vpmaxsq"
    "vpmaxsw" "vpmaxub" "vpmaxud" "vpmaxuq" "vpmaxuw" "vpminsb" "vpminsd"
    "vpminsq" "vpminsw" "vpminub" "vpminud" "vpminuq" "vpminuw" "vpmovb2m"
    "vpmovd2m" "vpmovdb" "vpmovdw" "vpmovm2b" "vpmovm2d" "vpmovm2q"
    "vpmovm2w" "vpmovmskb" "vpmovq2m" "vpmovqb" "vpmovqd" "vpmovqw"
    "vpmovsdb" "vpmovsdw" "vpmovsqb" "vpmovsqd" "vpmovsqw" "vpmovswb"
    "vpmovsxbd" "vpmovsxbq" "vpmovsxbw" "vpmovsxdq" "vpmovsxwd" "vpmovsxwq"
    "vpmovusdb" "vpmovusdw" "vpmovusqb" "vpmovusqd" "vpmovusqw" "vpmovuswb"
    "vpmovw2m" "vpmovwb" "vpmovzxbd" "vpmovzxbq" "vpmovzxbw" "vpmovzxdq"
    "vpmovzxwd" "vpmovzxwq" "vpmsub132ph" "vpmsub132sh" "vpmsub213ph"
    "vpmsub213sh" "vpmsub231ph" "vpmsub231sh" "vpmuldq" "vpmulhrsw"
    "vpmulhuw" "vpmulhw" "vpmulld" "vpmullq" "vpmullw" "vpmultishiftqb"
    "vpmuludq" "vpnmadd132sh" "vpnmadd213sh" "vpnmadd231sh" "vpnmsub132sh"
    "vpnmsub213sh" "vpnmsub231sh" "vpopcntb" "vpopcntd" "vpopcntq" "vpopcntw"
    "vpor" "vpord" "vporq" "vpperm" "vprold" "vprolq" "vprolvd" "vprolvq"
    "vprord" "vprorq" "vprorvd" "vprorvq" "vprotb" "vprotd" "vprotq" "vprotw"
    "vpsadbw" "vpscatterdd" "vpscatterdq" "vpscatterqd" "vpscatterqq"
    "vpshab" "vpshad" "vpshaq" "vpshaw" "vpshlb" "vpshld" "vpshldd" "vpshldq"
    "vpshldvd" "vpshldvq" "vpshldvw" "vpshldw" "vpshlq" "vpshlw" "vpshrdd"
    "vpshrdq" "vpshrdvd" "vpshrdvq" "vpshrdvw" "vpshrdw" "vpshufb"
    "vpshufbitqmb" "vpshufd" "vpshufhw" "vpshuflw" "vpsignb" "vpsignd"
    "vpsignw" "vpslld" "vpslldq" "vpsllq" "vpsllvd" "vpsllvq" "vpsllvw"
    "vpsllw" "vpsrad" "vpsraq" "vpsravd" "vpsravq" "vpsravw" "vpsraw"
    "vpsrld" "vpsrldq" "vpsrlq" "vpsrlvd" "vpsrlvq" "vpsrlvw" "vpsrlw"
    "vpsubb" "vpsubd" "vpsubq" "vpsubsb" "vpsubsw" "vpsubusb" "vpsubusw"
    "vpsubw" "vpternlogd" "vpternlogq" "vptest" "vptestmb" "vptestmd"
    "vptestmq" "vptestmw" "vptestnmb" "vptestnmd" "vptestnmq" "vptestnmw"
    "vpunpckhbw" "vpunpckhdq" "vpunpckhqdq" "vpunpckhwd" "vpunpcklbw"
    "vpunpckldq" "vpunpcklqdq" "vpunpcklwd" "vpxor" "vpxord" "vpxorq"
    "vrangepd" "vrangeps" "vrangesd" "vrangess" "vrcp14pd" "vrcp14ps"
    "vrcp14sd" "vrcp14ss" "vrcp28pd" "vrcp28ps" "vrcp28sd" "vrcp28ss"
    "vrcpph" "vrcpps" "vrcpsh" "vrcpss" "vreducepd" "vreduceph" "vreduceps"
    "vreducesd" "vreducesh" "vreducess" "vrndscalepd" "vrndscaleps"
    "vrndscalesd" "vrndscaless" "vroundpd" "vroundps" "vroundsd" "vroundss"
    "vrsqrt14pd" "vrsqrt14ps" "vrsqrt14sd" "vrsqrt14ss" "vrsqrt28pd"
    "vrsqrt28ps" "vrsqrt28sd" "vrsqrt28ss" "vrsqrtph" "vrsqrtps" "vrsqrtsh"
    "vrsqrtss" "vscalefpd" "vscalefph" "vscalefps" "vscalefsd" "vscalefsh"
    "vscalefss" "vscatterdpd" "vscatterdps" "vscatterpf0dpd" "vscatterpf0dps"
    "vscatterpf0qpd" "vscatterpf0qps" "vscatterpf1dpd" "vscatterpf1dps"
    "vscatterpf1qpd" "vscatterpf1qps" "vscatterqpd" "vscatterqps"
    "vshuff32x4" "vshuff64x2" "vshufi32x4" "vshufi64x2" "vshufpd" "vshufps"
    "vsqrtpd" "vsqrtph" "vsqrtps" "vsqrtsd" "vsqrtsh" "vsqrtss" "vstmxcsr"
    "vsubpd" "vsubph" "vsubps" "vsubsd" "vsubsh" "vsubss" "vtestpd" "vtestps"
    "vucomisd" "vucomish" "vucomiss" "vunpckhpd" "vunpckhps" "vunpcklpd"
    "vunpcklps" "vxorpd" "vxorps" "vzeroall" "vzeroupper" "wbinvd" "wbnoinvd"
    "wrfsbase" "wrgsbase" "wrmsr" "wrmsrlist" "wrmsrns" "wrpkru" "wrshr"
    "wrssd" "wrssq" "wrussd" "wrussq" "xabort" "xadd" "xbegin" "xbts" "xchg"
    "xcryptcbc" "xcryptcfb" "xcryptctr" "xcryptecb" "xcryptofb" "xend"
    "xgetbv" "xlat" "xlatb" "xor" "xorpd" "xorps" "xresldtrk" "xrstor"
    "xrstor64" "xrstors" "xrstors64" "xsave" "xsave64" "xsavec" "xsavec64"
    "xsaveopt" "xsaveopt64" "xsaves" "xsaves64" "xsetbv" "xsha1" "xsha256"
    "xstore" "xsusldtrk" "xtest")
  "NASM 2.17rc0 instruction tokens for `nasm-mode'.")

(defconst nasm-operator
  '("!=" "%%" "&&" "//" "<<" "<<<" "<=" "<=>" "<>" "==" ">=" ">>" ">>>" "?"
    "^^" "||")
  "NASM 2.17rc0 operator tokens for `nasm-mode'.")

(defconst nasm-pp-directive
  '("%aliases" "%arg" "%assign" "%clear" "%defalias" "%define" "%defstr"
    "%deftok" "%depend" "%elif" "%elifctx" "%elifdef" "%elifdefalias"
    "%elifdifi" "%elifempty" "%elifenv" "%elifid" "%elifidn" "%elifidni"
    "%elifmacro" "%elifn" "%elifnctx" "%elifndef" "%elifndefalias"
    "%elifndifi" "%elifnempty" "%elifnenv" "%elifnid" "%elifnidn"
    "%elifnidni" "%elifnmacro" "%elifnnum" "%elifnstr" "%elifntoken"
    "%elifnum" "%elifnusable" "%elifnusing" "%elifstr" "%eliftoken"
    "%elifusable" "%elifusing" "%else" "%endif" "%endm" "%endmacro" "%endrep"
    "%error" "%exitmacro" "%exitrep" "%fatal" "%iassign" "%idefalias"
    "%idefine" "%idefstr" "%ideftok" "%if" "%ifctx" "%ifdef" "%ifdefalias"
    "%ifdifi" "%ifempty" "%ifenv" "%ifid" "%ifidn" "%ifidni" "%ifmacro"
    "%ifn" "%ifnctx" "%ifndef" "%ifndefalias" "%ifndifi" "%ifnempty"
    "%ifnenv" "%ifnid" "%ifnidn" "%ifnidni" "%ifnmacro" "%ifnnum" "%ifnstr"
    "%ifntoken" "%ifnum" "%ifnusable" "%ifnusing" "%ifstr" "%iftoken"
    "%ifusable" "%ifusing" "%imacro" "%include" "%ipathsearch" "%irmacro"
    "%istrcat" "%istrlen" "%isubstr" "%ixdefine" "%line" "%local" "%macro"
    "%null" "%pathsearch" "%pop" "%pragma" "%push" "%rep" "%repl" "%require"
    "%rmacro" "%rotate" "%stacksize" "%strcat" "%strlen" "%substr" "%undef"
    "%undefalias" "%unimacro" "%unmacro" "%use" "%warning" "%xdefine")
  "NASM 2.17rc0 pp-directive tokens for `nasm-mode'.")

(defconst nasm-prefix
  '("a16" "a32" "a64" "asp" "bnd" "evex" "lock" "nobnd" "o16" "o32" "o64"
    "osp" "rep" "repe" "repne" "repnz" "repz" "rex" "times" "vex" "vex2"
    "vex3" "wait" "xacquire" "xrelease")
  "NASM 2.17rc0 prefix tokens for `nasm-mode'.")

(defconst nasm-register
  '("ah" "al" "ax" "bh" "bl" "bnd0" "bnd1" "bnd2" "bnd3" "bp" "bpl" "bx" "ch"
    "cl" "cr0" "cr1" "cr10" "cr11" "cr12" "cr13" "cr14" "cr15" "cr2" "cr3"
    "cr4" "cr5" "cr6" "cr7" "cr8" "cr9" "cs" "cx" "dh" "di" "dil" "dl" "dr0"
    "dr1" "dr10" "dr11" "dr12" "dr13" "dr14" "dr15" "dr2" "dr3" "dr4" "dr5"
    "dr6" "dr7" "dr8" "dr9" "ds" "dx" "eax" "ebp" "ebx" "ecx" "edi" "edx"
    "es" "esi" "esp" "fs" "gs" "k0" "k1" "k2" "k3" "k4" "k5" "k6" "k7" "mm0"
    "mm1" "mm2" "mm3" "mm4" "mm5" "mm6" "mm7" "r10" "r10b" "r10d" "r10w"
    "r11" "r11b" "r11d" "r11w" "r12" "r12b" "r12d" "r12w" "r13" "r13b" "r13d"
    "r13w" "r14" "r14b" "r14d" "r14w" "r15" "r15b" "r15d" "r15w" "r8" "r8b"
    "r8d" "r8w" "r9" "r9b" "r9d" "r9w" "rax" "rbp" "rbx" "rcx" "rdi" "rdx"
    "rsi" "rsp" "segr6" "segr7" "si" "sil" "sp" "spl" "ss" "st0" "st1" "st2"
    "st3" "st4" "st5" "st6" "st7" "tmm0" "tmm1" "tmm2" "tmm3" "tmm4" "tmm5"
    "tmm6" "tmm7" "tr0" "tr1" "tr2" "tr3" "tr4" "tr5" "tr6" "tr7" "xmm0"
    "xmm1" "xmm10" "xmm11" "xmm12" "xmm13" "xmm14" "xmm15" "xmm16" "xmm17"
    "xmm18" "xmm19" "xmm2" "xmm20" "xmm21" "xmm22" "xmm23" "xmm24" "xmm25"
    "xmm26" "xmm27" "xmm28" "xmm29" "xmm3" "xmm30" "xmm31" "xmm4" "xmm5"
    "xmm6" "xmm7" "xmm8" "xmm9" "ymm0" "ymm1" "ymm10" "ymm11" "ymm12" "ymm13"
    "ymm14" "ymm15" "ymm16" "ymm17" "ymm18" "ymm19" "ymm2" "ymm20" "ymm21"
    "ymm22" "ymm23" "ymm24" "ymm25" "ymm26" "ymm27" "ymm28" "ymm29" "ymm3"
    "ymm30" "ymm31" "ymm4" "ymm5" "ymm6" "ymm7" "ymm8" "ymm9" "zmm0" "zmm1"
    "zmm10" "zmm11" "zmm12" "zmm13" "zmm14" "zmm15" "zmm16" "zmm17" "zmm18"
    "zmm19" "zmm2" "zmm20" "zmm21" "zmm22" "zmm23" "zmm24" "zmm25" "zmm26"
    "zmm27" "zmm28" "zmm29" "zmm3" "zmm30" "zmm31" "zmm4" "zmm5" "zmm6"
    "zmm7" "zmm8" "zmm9")
  "NASM 2.17rc0 register tokens for `nasm-mode'.")

(defconst nasm-size
  '("byte" "dword" "oword" "qword" "tword" "word" "yword" "zword")
  "NASM 2.17rc0 size tokens for `nasm-mode'.")

(defconst nasm-special
  '("abs" "dup" "far" "long" "near" "nosplit" "ptr" "rel" "seg" "short"
    "strict" "to" "wrt")
  "NASM 2.17rc0 special tokens for `nasm-mode'.")
