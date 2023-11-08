/* Copyright (c) 2023 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <string_view>

#include "base/containers/contains.h"
#include "base/containers/fixed_flat_set.h"
#include "brave/components/brave_ads/core/internal/common/resources/language_components.h"

namespace {

constexpr auto kLanguageComponentIds = base::MakeFixedFlatSet<std::string_view>(
    {"kogodakapiaolmflecdeoopfboigagoa", "ngfmddfblpjplgicnjgcddgpfdmfhapc",
     "kdpaehcighoijicnehaocfopdeochjif", "ajbmmckjjefephnpbfkclfbgpdblnblh",
     "deagihkbpkgnjmhhknnnaafigcaigila", "nagkhogdiimjlkdjgdfkmedofekhhama",
     "fdfhocbndfmbaaiknbfjnggkhikblidh", "fcclkkceoojoglcaibjepgpbjkdhlpfo",
     "onpgbfbegddfenanmbbffghlcjekoofh", "higolihinmajgkogngcidjbfdoiihamm",
     "fkjilonpflalicocekbmdofheobkojlb", "hmepmpkmommnhoipcehaofblmncnggcf",
     "empmcokjpnmnidnfoljghcfbgeblalgp", "lobeidgjjjgdaniolkehacndaicnfiej",
     "gikemmfigjfpjblncmdlnpegljihcigp", "jniigbhankenmmknhlghiehlelodnedn",
     "pbhnlaihbgaglianjhjbndgkmbgnemlc", "gdegmeololfdgdgelkdaceijogfnnack",
     "khlloiikgplkcknkggahdapdmcglccob", "hijldpjimjlofaaokclbgmjhglffiemc",
     "bbpfgffhbkgibgaoimodiohnldookmaa", "iinljbfhbkedipcjhppphhpdimdaadnh",
     "ahoikcjlabbcjfdmaklkbkahkhobdpoe", "akblniopafhkclafncllbcljihhkakff",
     "jghkdegecmffchogjidkldfibcojjmbf", "ciaoccdjkendldgbledhldfnbekafmkj",
     "ojckbcpicdkpppdfdahmmphhnhagekkc", "micnhmggekafdcmkjenlnpohmjnpfpok",
     "jaojemeamojmkhhgcfbbjhjliaimjjhj", "deooomackoaaifhnfpafjjadijlmnijl",
     "emdnhcpfiggffkofmbdnopbkkndplbfb", "onppklmkaefmphnkclejnjaokaifdfla",
     "jpiohinjiligakfhlfnaficklbdgoapb", "gngoidkmjlhckdcjmakgbmbobaeoflli",
     "ibglgicbkikbdaohfnlhfphcmimemgip", "hbfbplkldbdpmpfkpekmcaabcehnbmgd",
     "mfedcgmmiaefddmdiipaihggolhbancb", "kglppciafpehhjdhgefiljcmemakfjhk",
     "pngfmkdibpddpimalodkeibanjplkfpg", "najeijhchapgojlmbpmgkbmojjemljkg",
     "iekmbanolemjaamkijghdajieakoelpl", "ijfjgbaoecajpdanolgfppfcnpnlipgd",
     "nfmiifnogalbcaiemnocgmnkmelclapc", "eahbjhjcmoccomcdkemlpikajndioohh",
     "lmbmkmpkmlcndkencpiiobeaemapefid", "kbojdaoifolinhclahfbfjlimlfbncdh",
     "jndgmdmflcoeoonmgildhdemonpfiebh", "cmiljbfgiidkhoibfmmljbipihfpimoh",
     "lkcejfmfaljlleokgmeiilcahmdodobn", "dhnegkecbcchnopfobppibeindhomfln",
     "lipiahgcofmdenojlondcepjdimpbdof", "noejkmlbjohohnmbenempdmdmlpalpib",
     "ffbbhglopkbkkamcdghhmnfnbmciomka", "bbjohelpalbnhbmecpfmfihnjjciamkc",
     "mkocecdaofokilgekmefhkdkpceobmoh", "lkcmkicgnnhpjpclheibdecfdgppapdl",
     "ihgbpogafgklooapikgebidklmmlbdjl", "obmelkjmjgbafmdimgggckgbmhdiekcf",
     "kcmhoeiigodckpjnempjpomjpgidddan", "mpfabogdnplgoibcaifmldkjjknfeenc",
     "ngljnkkbehhdmgjhgapjhabgdlfhlmpb", "loadmnlmeliphfgohiddbajjdahpgake",
     "bhhegfbiepacbklcjanlkgdagipoeean", "daofedggphmenhbdcoedmghhhepdlpmi",
     "khockgajcgemepmfciianhddkeejdkce", "ppjdjjifcklgafgnfnifinlbahapjknh",
     "hnckkloeiedfalgconmhonbkgfogcmbm", "ldpbnbodnegddenfcbjkcomhbeknohnf",
     "hpninadkakmbkkmmefifcfmolbeogibp", "njpplkmalfbgkgjfiegleckhdombnnka",
     "jjfkpjincjmlbhncijpdjcpafdchfdgc", "pcclcjakekdpbgpmliidnlphfijbnjbo",
     "gmbdpfbonijljlmmonmclcfncglcabge", "kjepaclmjbolindcadnclbmpnlddclcm",
     "bpadoekcogchnjlaijgoochmhankegjm", "dcofgggdddmcpmkgokniklkhcnnogcae",
     "fmggnbcjicdmpemlpbcikgiglkkjfjkm", "acfdgepkfkokgehdlgaghinaonidaegc",
     "enepjnajfhpoiiljineppklcpigibbab", "cfeojpgnnoehnejgaoildnannidchpog",
     "jcmhejmogglbjhaloeapahkeeahefkjh", "lnoncinlkehheipmonleiofgldekknnh",
     "mgelodghgdfbbfaadcpcbecbkgnakcek", "gpnahkganbnkacfdmepgbeablddkpgop",
     "njpfhoifmbhakegfpccjapncklgibbef", "ggmiobkijiikpdfhajmbbcmnnacnpnoo",
     "lbgapnaokhcmhngommhbibimcnigflmk", "hcjogcekdolmcdfibaeddoekinohmbnf",
     "lpiemhfjklcklennlodgcdafbeipenim", "hlheafkmfelbenhhhplpdjjeiaeicnmm",
     "facibffkakgonanadmpddcdfjdnfhihh", "edgeoahnaojcgpaaaoldhhkdoaocejal",
     "gakjihllcbgpnioilgfghibnlokepmna", "odgegiadolgfpbdkibpbaohljnkhlfpb",
     "lmojijickbahpkocakkdpgjmidfimmki", "fgapmpgdnpkikmgoaejicodcleolamhp",
     "dcdajpkfeohcmfegdnjckbjacggohmcc", "bjhngpmfhibbobclecjnbamcankikopn",
     "dhpdkbhhcaddlmjagifnbpimfbfijnha", "ldbkgffjalniiddeknjocmdpancalckl",
     "jfkhehdjnjiblekbbkgjaneaacbaafnp", "mgpggidcfemhhogedejilfglocpiekjc",
     "jokcjbkdpnodbnecicmogogknankkpnj", "hldegkdhfdmkgfimpjcgfadaclckfmfd",
     "nbnencalecdgoibdjikokpcljoobjcjp", "illemcnpmakoomaoaimplegenbnkdnco",
     "dnpkcngeojlgkpcogmbhngpiloelndnl", "omlgbfengmliklfmgpohbbehifpbhlhd",
     "mmhbcheepgacccpkoojgogkpeclnhaki", "bhlginalhkcjcbfenobaobbbcedpmcpo",
     "aillcomncineklhmcfplfifnkobjphhm", "ggchkofgeacaidohpgmndanhgndpiand",
     "encfbmnopagbjhbciljekjijpcoiedfa", "pogkiaagmhbjlocoogleepkhpfbkepjb",
     "ijkadkdgolnlhifajonekidhmclpnimh", "bildgobahnefhoaffjbemcicllcmhgee",
     "fkkbblhloenjcajbmlppinmakemaeola", "lbjalogogllmhpjmgpiaibbejekcnckf",
     "fgnoadecmnciafekngpfcfhigfoficel", "oakdddlopedcghebikdjfejlminmhgml",
     "mkpcckpjlmjdimnlpdeimcpcnahbmjnh", "hhagmoncpcffgomhooclincbkkjfgcle",
     "ibblamcocacknfpcjoeoelgbdhnmfagn", "bcenhajcipdelhkmccpdiefedjohogba",
     "neiomohhafbpmhhhinhgjpbjbckmcebe", "gppimcefaccichflfljoegdfhpodnnij",
     "lfehjamnokfbkbcbnpindigjdimfefaj", "jdaihiahjeapdgbbgjkijfenhkfipmio",
     "bhpcdajlgegdnpmemhpaecokokoooobd", "gfmhlmdgmkcgljbnppimebiplocifhik",
     "aojkcibdeijcgknbfichefcabhcgegcc", "fdnikmpkpjbleckemlefagljgjiihfdi",
     "hmngcjbeefhdchoojpngdhnhbbeanpnl", "mhnbapbhkbeeeapjppocinifblejchaa",
     "coihjoimplgfdefpneebcibpacbdgpjg", "lpodncbdknlofkodbidgifgmkknifmla",
     "ojjkbmpholapahjkdbenchbjhonldigc", "gkhfiahfninjaijfcbdpnciegjillled",
     "gjmafipnhiecebhnhelhmoijpejkpand", "gfgghgbencnpeclofdkpmcjlomnleidk",
     "gimpnlcjockckkbmchlmmdekinhpjmfi", "ndlbmngagmpalpcekppiedhoamhblole",
     "ffnhfncfeckidgidaakgljfdggmoabpd", "offghhockdeeikkgcoepfpnjkppfecem",
     "anacmihbcabiejodiilhcnfmdfdikaoe", "mhpagiieapkfnhnafgjkfnhnjmfocaic",
     "cebnoofnafbfjiimpkebljfpbodgcjhg", "gjmcokanonhcninlfooigncakhllaikn",
     "aeihoelfbibcfnbpnmkkfokkgempgiln", "hagjngdelecbcdpijmodiahlnmmgfjfi",
     "ibfonpegmejcbjpfamoafdgejihgoknj", "cfckindapnbjggncmhafocnkcfldaalb",
     "ikidfnelkabogjadaeallopffafdbfla", "djmcpnielgbphhonabngpeciidhhgnkl",
     "efghmckimcmoiobakclfgabokpkmhalf", "gmddkgjgeaadjgmmofoabhjaoaccchec",
     "lonpickpbpbipenlmiibhaplidbilllj", "nkfkflngeogepphdejemlobijhloddpc",
     "cpeechlpffbeodnaadbojebpnkoheipf", "gjndgegkohodefegcdcikmnikpopddja",
     "efblhgffhcdpkmhijlaohfiglcanopgl", "oaglbomehfcjahpnghnpaecafmppejlj",
     "beccjhpgbfkaaacmaakccepfogpnhnig", "algfapbflpgohacbgnjfiihgkkfkbefh",
     "mflockmaebeiekfgbkdaooliggmedaei", "gbgmodfnkdingfnhfmijlkocekcbmcmb",
     "ilcgjpmbbbeacabmhcbmgnfiedagopkg", "njfflilceibpdamnnenjbkdmpeipekjl",
     "fejbdakfdmhkjccankjfmalinbimehld", "lmdaaoicfkfghlkdgeeanhejppmnibog",
     "hilkjgnimmpbgngiooegakodlhgfadpk", "djiaaecplnaellbiieeoofocfbfgmmfo",
     "hgmaccdpcfoccdfdbjkbohdbcnnmgjki", "gcpfgdghceojiadkoablgemdgkcmedem",
     "hmcpldmlocckkpebnpfbbcnegjhdebok", "bbpkdlofgljjgemgdgebkeaeaccdmfea",
     "nkffcfefebphjhelfpgflfijfhgalmip", "egbkpickminicpgpohgcdkkpllipfnhm",
     "dneffikodaplblpicljlfiokpiolicgb", "mehlnebobppohgkocbmeemacmojhcngl",
     "oaombnbnionmappopndanpmfbledkmfe", "aigpjmdeekliddeepajicchfmfbeckjn",
     "bdhhcikcnemalcgpplhjndkbhobkakcb", "gjjbfjgkglcbhckpmhoendlifmemaekd"});

}  // namespace

namespace brave_ads {

bool IsValidLanguageComponentId(const std::string& id) {
  return base::Contains(kLanguageComponentIds, id);
}

}  // namespace brave_ads
