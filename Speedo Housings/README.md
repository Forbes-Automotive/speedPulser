# SpeedPulser speedo housings

Motor housings that hold the SpeedPulser / SpeedPulser Pro motor on the back of a speedo. They're shared by both products: the housing depends on the speedo, not the board.

Every file is named `sp_speedo_<car or thread>_<fitting>.stl`. The superseded versions are in `old\`. Nothing has been deleted. Copied and renamed on 27 Oct 2026.

## Current housings

| Folder | File | Fits | Body | GitHub | Firmware calibration |
|---|---|---|---|---|---|
| `vw` | `sp_speedo_vw-golf_clip-on.stl` | MK1 / MK2 Golf, Scirocco, Jetta (OEM clip-on spigot) | 50 mm square flange | Yes | Yes (6 profiles) |
| `vw` | `sp_speedo_vw-golf_threaded.stl` | Golf clusters with a threaded cable collar (V3) | Ø47 round | Older version only | Golf profiles |
| `vw` | `sp_speedo_vw-golf_screw-lock.stl` | Screw-lock speedo: Ø9.6 sleeve, 8 mm square tang (needs `sp_coupler_screw-lock`) | 50 mm square flange | No | Golf profiles, not yet test-fitted |
| `vw` | `sp_speedo_vw-golf_shield.stl`, `sp_speedo_vw-golf_basic-box.stl`, `sp_speedo_vw-mk2_cluster-support.stl` | Golf accessories (shield, basic box, MK2 cluster support) | | Shield and box: yes | |
| `mercedes` | `sp_speedo_merc-w123.stl` | Mercedes W123 | Ø47 round | Yes | Yes |
| `mercedes` | `sp_speedo_merc-w124.stl` | Mercedes W124 | Ø47 round | Yes | **No W124 profile** |
| `mercedes` | `sp_speedo_merc-w124_long-neck.stl` | W124, longer neck (one customer's car) | Ø47 round | No | No |
| `smiths` | `sp_speedo_smiths_5-8in-unf_wide.stl` / `_narrow.stl` | Smiths, 5/8" UNF thread | 50 mm square / Ø47 round | Yes | Yes (70 / 90 / 150 mph) |
| `smiths` | `sp_speedo_smiths_m12.stl` | Smiths, M12 thread | Ø47 round | No | Smiths profiles |
| `volvo` | `sp_speedo_volvo_m12x1-0_wide.stl` / `_narrow.stl` | Volvo, M12×1.0 thread | Ø47 round | Narrow only | No |
| `volvo` | `sp_speedo_volvo_m18x1-5.stl` | Volvo and other M18×1.5 speedos | Ø47 round, extended nose | Yes | No |
| `volvo\vdo-jet-cockpit` | `_housing.stl`, `_nut.stl`, `_drive-key.stl` | Volvo VDO "jet cockpit" cluster (new, 27 Oct): housing, threaded nut and drive key | Ø47 round, 89 mm long | No | No |
| `volvo` | `twist-lock_NOTES.md` | Volvo VDO twist-lock. Notes only: the housing is still being measured | | | |
| `jaguar` | `sp_speedo_jaguar_15-32in.stl` | Jaguar, 15/32" (11.91 mm) thread | Ø47 round, 85 mm long | No | No |
| `porsche` | `sp_speedo_porsche.stl` | Porsche | Ø47 round, 85 mm long | No | No |
| `ford` | `sp_speedo_ford.stl`, `sp_speedo_ford-escort.stl` | Ford, Ford Escort | 50 mm square flange | Yes | Escort: yes |
| `ford` | `sp_speedo_ford-transit-mk3.stl` | Ford Transit MK3 | 50 mm square flange | No | No |
| `ford` | `post-mount_NOTES.md` | Ford post-mount, two-part version. Notes only: no STLs yet | | | |
| `fiat` | `sp_speedo_fiat-uno.stl` | Fiat Uno (V2) | 50 mm square flange | Yes | Yes |
| `isuzu` | `sp_speedo_isuzu.stl` | Isuzu | 50 mm square flange | No | No |
| `delorean` | `sp_speedo_delorean_thread-insert.stl` | DeLorean DMC-12. This is an insert, not a full housing | Ø19 insert | No | No |
| `reliant` | `sp_speedo_reliant-scimitar.stl` + `_drive-peg.stl` | Reliant Scimitar (community design, from GitHub) | Ø46 round | Yes | No |
| `common` | `sp_coupler.stl` / `.step`, `sp_coupler_screw-lock.stl` | Drive coupler used in every housing (6 mm D to the motor, 2.85 mm square to the speedo); screw-lock version for the 8 mm tang | Ø12 × 14.3 | Coupler: yes | |
| `accessories` | `sp_clip_v3.stl`, `sp_clip_v3_tab.stl` | Clip, latest version, with and without the tab | | No | |
| `scripts` | `adapt_mk1_screwlock.py`, `build_screwlock_adapter.py` | Scripts that generated the screw-lock housing and coupler | | | |

## old

| File | Replaced by |
|---|---|
| `sp_speedo_fiat-uno_v1.stl` | `fiat\sp_speedo_fiat-uno.stl` (V2) |
| `sp_speedo_delorean_thread-insert_v1.stl` | `delorean\...thread-insert.stl` (v2) |
| `sp_speedo_vw-golf_threaded_v1.stl`, `_v2.stl`, `sp_speedo_threaded_early.stl` | `vw\sp_speedo_vw-golf_threaded.stl` (V3) |
| `sp_speedo_isuzu_pre-final.stl`, `sp_speedo_ford-transit-mk3_pre-final.stl` | The FINAL versions (identical size; kept just in case) |
| `sp_speedo_vw-golf_basic-box_v1.stl` | `vw\...basic-box.stl` (V2) |
| `sp_clip_v1*.stl`, `sp_clip_v2*.stl` | `accessories\sp_clip_v3*.stl` |
| `spp_pcb-lid_*_early.stl`, `spp_pcb-support_early.stl` | The SpeedPulser Pro V3 enclosure in `Enclosures\speedPulserProV3` |
| `sp_freq_box_early.stl` | Early SpeedPulser frequency board box |

Not copied: the MK4 Golf xenon adapters and the swabbing rope piece. They aren't SpeedPulser parts.

## To do

- Put the missing housings on GitHub (Forbes-Automotive/speedPulser, `Motor Support Model\`): Jaguar, Porsche, Smiths M12, Volvo M12 wide, Volvo VDO jet cockpit, Golf threaded V3, Golf screw-lock, Isuzu, Transit, DeLorean.
- Add firmware calibrations for Mercedes W124 and the Volvo, Jaguar and Porsche housings once each has been fitted.
- On GitHub, `VolvoSpeedoAdapter-M12x1.0Narrow.STL` is also in the Smiths folder. Remove that copy.
