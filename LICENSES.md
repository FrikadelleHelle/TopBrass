# Code, recordings and artwork

TopBrass source code and original interface artwork are licensed under GNU AGPL version 3 (see LICENSE). Copyright 2026 TopBrass contributors. The artwork's generated-image provenance is documented in assets/ui/README.md.

JUCE is a separate dependency, pinned by scripts/bootstrap.sh. It is available under AGPLv3 or the JUCE commercial license; this public distribution uses the AGPLv3 route. Its license and third-party dependency inventory are in vendor/JUCE after bootstrap. Binary ZIPs include the JUCE dependency inventory and notice files; the release also includes a source archive of the exact JUCE revision. Release source includes our complete code and build scripts; bootstrap obtains the pinned JUCE source.

Recordings are separately licensed assets. The code license does not replace their terms:

| Source | License | Credits |
| --- | --- | --- |
| VCSL and VSCO 2 Community Edition | CC0 1.0 | Versilian Studios, Sam Gossner, Simon Dalzell and credited performers |
| Bear Sax and War Tuba | CC0 1.0 | Karoryfer Samples; War Tuba performed by Jakub Lewicki and recorded/mapped by Drogomir Smolken |
| MTG Solo Saxophones | CC BY 4.0 | Music Technology Group, Universitat Pompeu Fabra; SFZ preparation by kinwie |
| Bell & Bone trumpet/trombone | CC BY-NC-SA 4.0 plus Libre Wave CC+ permissions | Libre Wave / David Healey |

Full license texts, attribution and processing descriptions are included in assets/plugin-resources and in every plugin bundle. Bell & Bone public maps retain their upstream license. TopBrass adapts their regions, tuning, gain, looping and releases for its playback engine. The sample-input archive contains the original four FLAC audio monoliths and public maps; it omits the original sampler software and binary header. Its extraction manifest retains the original archive hash and individual audio hashes.

The supplied Bell & Bone recordings and their adaptations are distributed for noncommercial purposes under their stated license and share-alike terms. Their additional permissions allow commercial musical works and certain composed loops/phrases; they do not give blanket permission to sell this multisample instrument. Read Bell-and-Bone-LICENSE.txt before redistributing or using the assets in another product. Other sample sources retain their own licenses independently.

Source changes and sample processing are described in the scripts and sample-bank manifests. The sample-input lock lists every distributed input's SHA-256; the upstream locks/maps retain commit and original-file provenance. The Bell archive parser is an original implementation referring to HISE's HlacArchiver format (https://github.com/christophhart/HISE), rather than distributing HISE's sampler.
