#pragma once

#include "EditTypes.h"

#include <optional>

class Scene;

/** @brief Applies editor mutations independently of keyboard, UI, or file-loading adapters. */
class EditorActions
{
  public:
    explicit EditorActions(Scene& scene) : m_scene(scene) {}

    /**
     * @brief Stages a complete batch and publishes it only after validation.
     * @param batch Ordered operations; placement and removed-pin checks use the final candidate.
     * @return Typed failure without mutation, or one complete change record; no-ops have no record.
     */
    EditResult apply(const EditBatch& batch);

    /** @brief Starts a presentation-only move preview, leaving committed topology unchanged. */
    std::optional<MovePreviewHandle> beginMove(int componentId);
    /** @brief Updates a matching preview without rebuilding or changing committed geometry. */
    bool previewMove(MovePreviewHandle handle, GridCoords position);
    /** @brief Commits the final preview position once, or cancels it on validation failure. */
    EditResult commitMove(MovePreviewHandle handle, bool reroute = true);
    /** @brief Discards a matching preview; committed geometry and topology remain untouched. */
    bool cancelMove(MovePreviewHandle handle);

    /**
     * @brief Restores a complete edit image and rebuilds derived connectivity once.
     * @param snapshot Before/after image from an EditRecord; allocation counters remain monotonic.
     * @param expectedRevision Current revision expected by the caller, preventing stale restores.
     * @return A new reversible change, or a typed failure without mutation.
     */
    EditResult restore(const Scene& snapshot, std::uint64_t expectedRevision);

  private:
    Scene& m_scene;
    EditResult applyImpl(const EditBatch& batch, bool allowPreview);
    EditResult publish(
        Scene candidate,
        std::shared_ptr<const Scene> before,
        std::vector<int> components = {},
        std::vector<WireId> wires = {}
    );
};
