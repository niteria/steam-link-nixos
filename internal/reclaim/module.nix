{
  stateNand,
  kernel,
  buildPackages,
  checks,
}:
stateNand.overrideAttrs (old: {
  pname = "steamlink-reclaim-guarded-state";
  nativeBuildInputs = old.nativeBuildInputs ++ [ buildPackages.python3 ];
  prePatch = old.prePatch + ''
    cp --remove-destination ${./state-policy.h} trial-policy.h
    cp --remove-destination ${./state-policy.h} state-policy.h
    cp ${./layout.h} layout.h
    cp ${./parts.h} reclaim-parts.h
    python3 ${./prepare-source.py}
  '';
  postPatch = old.postPatch + ''
    substituteInPlace Makefile --replace-fail steamlink_marvell_state steamlink_marvell_reclaim
    substituteInPlace drivers/mtd/nand/raw/marvell_nand.c \
      --replace-fail '"steamlink-marvell-internal-state"' '"steamlink-marvell-reclaim"'
  '';
  preBuild = old.preBuild + ''
    test -e ${checks}/result.txt
  '';
  installPhase = ''
    mkdir -p "$out/lib/modules/${kernel.modDirVersion}/extra"
    cp steamlink_marvell_reclaim.ko steamlink_nand_overlay.ko "$out/lib/modules/${kernel.modDirVersion}/extra/"
    cp nand.dtbo "$out/"
  '';
  passthru.tests.reclaimBounds = checks;
  meta = old.meta // {
    description = "Explicitly gated cache/sysconf/factory state writer; stock boot, identity, root, recovery and BBT excluded";
  };
})
