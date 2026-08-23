#pragma once

namespace App::Debug {

/// Confidence vocabulary used selectively for reverse-engineered semantics.
enum class EvidenceConfidence {
  k_confirmed_runtime,
  k_confirmed_data,
  k_corroborated,
  k_reconstructed,
  k_provisional,
  k_open_nomad_only,
  k_unknown,
};

/// Compact label suitable for inspector annotations.
[[nodiscard]] constexpr const char* evidence_label(const EvidenceConfidence confidence) {
  switch (confidence) {
    case EvidenceConfidence::k_confirmed_runtime:
      return "Confirmed - Runtime";
    case EvidenceConfidence::k_confirmed_data:
      return "Confirmed - data";
    case EvidenceConfidence::k_corroborated:
      return "Corroborated";
    case EvidenceConfidence::k_reconstructed:
      return "Reconstructed";
    case EvidenceConfidence::k_provisional:
      return "Provisional";
    case EvidenceConfidence::k_open_nomad_only:
      return "OpenNomad-only";
    case EvidenceConfidence::k_unknown:
      return "Unknown";
  }
  return "Unknown";
}

}  // namespace App::Debug
