#pragma once
#include "support/CoreMath.h"
#include "session/SessionRuntime.h"
#include "data/WorldData.h"
#include "render/World.h"
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include <cstddef>

#define CUSTOM_CAMERA_DISTANCE1 200
#define CUSTOM_CAMERA_DISTANCE2 -150
#define RENDER_ITEMVIEW_FAR 2000.f
#define RENDER_ITEMVIEW_NEAR 20.f

/// Convert horizontal FOV (degrees) to vertical FOV (degrees) for gluPerspective.
/// hFovDeg is the horizontal FOV at the 4:3 reference aspect. The `aspectRatio`
/// argument is intentionally ignored — we want vFov tied to the reference
/// aspect so wider resolutions extend the side view via gluPerspective rather
/// than stretching or cropping vertically. The UI is still scaled to actual
/// aspect separately (g_fScreenRate_x/y) which is the intended behavior there.
inline float HFovToVFov(float hFovDeg, float /*aspectRatio*/)
{
    constexpr float PI = 3.14159265358979323846f;
    constexpr float REFERENCE_ASPECT = 4.0f / 3.0f;
    float hHalfRad = hFovDeg * 0.5f * PI / 180.0f;
    float vHalfRad = atanf(tanf(hHalfRad) / REFERENCE_ASPECT);
    return vHalfRad * 2.0f * 180.0f / PI;
}

/**
 * @brief Rendering distance multiplier applied to camera far plane
 *
 * This multiplier extends the OpenGL projection matrix far plane beyond the camera's
 * logical ViewFar to prevent pop-in artifacts at screen edges. The multiplier creates
 * a buffer zone where terrain/objects slightly outside the frustum are still rendered.
 *
 * Used by:
 * - BeginOpengl() for projection matrix setup (ZzzOpenglUtil.cpp)
 * - ScreenToWorldRay() for mouse picking ray distance (CameraProjection.cpp)
 * - ForMainScene() for terrain culling range calculation (CameraConfig.h)
 *
 * @note Value of 1.4 provides 40% buffer (e.g., 1700 → 2380 units)
 */
constexpr float RENDER_DISTANCE_MULTIPLIER = 1.4f;

/**
 * @brief Fixed camera transform used in CharacterScene
 *
 * CharacterScene uses a static hardcoded camera pose (the original game didn't move
 * the camera on this screen). Both DefaultCamera and OrbitalCamera need these values:
 * DefaultCamera snaps to them directly, OrbitalCamera uses them as a ray-cast origin
 * to compute where to place the orbit pivot.
 */
namespace CharacterSceneCamera
{
constexpr float POSITION_X = 9758.93f;
constexpr float POSITION_Y = 18913.11f;
constexpr float POSITION_Z = 675.5f;
constexpr float ANGLE_PITCH = -84.5f; // Angle[0]
constexpr float ANGLE_ROLL = -75.0f;  // Angle[2]
} // namespace CharacterSceneCamera

/**
 * @brief Camera rendering configuration
 *
 * Encapsulates all parameters that define how a camera renders the scene.
 * Replaces hardcoded scene-specific frustum values with clear, configurable parameters.
 */
struct CameraConfig
{
    // ========== View Frustum Parameters ==========

    /** Horizontal field of view in degrees. Vertical FOV is derived at runtime via HFovToVFov(hFov, aspect). */
    float hFov = 90.0f;

    /** Near clipping plane distance */
    float nearPlane = 10.0f;

    /** Far clipping plane distance */
    float farPlane = 2400.0f;

    /**
     * Aspect ratio (width / height) - calculated dynamically from viewport dimensions
     *
     * This value is NOT used directly! BeginOpengl() calculates aspect ratio every frame
     * from the current viewport (Width / Height) and passes it to SetupPerspective().
     * This field is kept for reference/compatibility but the live aspect comes from window size.
     *
     * Window resizing (WM_SIZE) automatically updates WindowWidth/WindowHeight, and the
     * next BeginOpengl() call will use the new dimensions to calculate the correct aspect ratio.
     */
    float aspectRatio = 1.33f;

    // ========== Culling Parameters ==========

    /**
     * Terrain culling range (2D ground projection far distance)
     * Controls how far terrain tiles are rendered
     */
    float terrainCullRange = 1100.0f;

    /**
     * Object culling range (3D frustum far distance)
     * Usually same as farPlane, but can be different for optimization
     */
    float objectCullRange = 2400.0f;

    // ========== Fog Parameters ==========

    /**
     * Fog start distance (where fog transition begins)
     * Default: 100% of farPlane
     */
    float fogStart = 2400.0f;

    /**
     * Fog end distance (where fog reaches full density)
     * Default: 125% of farPlane
     */
    float fogEnd = 3000.0f;

    // ========== Comparison Operator ==========

    bool operator==(const CameraConfig &other) const
    {
        return hFov == other.hFov && nearPlane == other.nearPlane && farPlane == other.farPlane &&
               aspectRatio == other.aspectRatio && terrainCullRange == other.terrainCullRange &&
               objectCullRange == other.objectCullRange && fogStart == other.fogStart &&
               fogEnd == other.fogEnd;
    }

    bool operator!=(const CameraConfig &other) const
    {
        return !(*this == other);
    }

    // ========== Preset Configurations ==========

    /**
     * @brief MainScene default camera configuration
     *
     * Optimized configuration for MainScene DefaultCamera.
     * Balanced for performance and visibility.
     *
     * Values (user-specified):
     * - FOV: 72 degrees
     * - Near Plane: 10
     * - Far Plane: 1700 (3D object culling)
     * - Terrain Cull Range: 1700 (2D terrain culling)
     */
    static CameraConfig ForMainSceneDefaultCamera()
    {
        CameraConfig config;
        config.hFov = 40.0f; // ~30° vFOV on 4:3, preserves original game look
        // Match the projection's near plane (g_Camera.ViewNear = 20). Higher
        // values here would CPU-cull objects that the GPU still renders,
        // making them vanish as they approach the camera.
        config.nearPlane = 20.0f;
        config.farPlane = 3000.0f;
        // Use RENDER_DISTANCE_MULTIPLIER to ensure terrain culling matches rendering/picking distance
        config.terrainCullRange = 3000.0f * RENDER_DISTANCE_MULTIPLIER; // = 4200.0f
        config.objectCullRange = 3000.0f;
        config.fogStart = config.farPlane * 1.0f;
        config.fogEnd = config.farPlane * 1.25f;
        return config;
    }

    /**
 * @brief MainScene orbital camera configuration
 *
 * Optimized configuration for MainScene OrbitalCamera.
 * Balanced for performance and visibility.
 *
 * Values:
 * - FOV: 90 degrees
 * - Near Plane: 20 (matches projection's ViewNear)
 * - Far Plane: 3800 (3D object culling)
 * - Terrain Cull Range: 5320 (farPlane * RENDER_DISTANCE_MULTIPLIER)
 */
    static CameraConfig ForMainSceneOrbitalCamera()
    {
        CameraConfig config;
        config.hFov = 40.0f; // Match Default cam so orbital looks identical at activation
        // Match the projection's near plane (g_Camera.ViewNear = 20). Higher
        // values here would CPU-cull objects the GPU still renders — visible
        // in zoomed-in orbital where the camera sits ~200 units from hero.
        config.nearPlane = 20.0f;
        config.farPlane =
            3800.0f; // Direct value — RENDER_DISTANCE_MULTIPLIER already applied in BeginOpengl()
        config.objectCullRange = 3800.0f;
        config.fogStart = config.farPlane * 1.0f;
        config.fogEnd = config.farPlane * 1.25f;
        config.terrainCullRange = config.farPlane * RENDER_DISTANCE_MULTIPLIER;
        return config;
    }

    /**
     * @brief Login scene camera configuration
     *
     * Massive panoramic view for the cinematic login screen.
     * Camera moves through the environment on a pre-defined path.
     *
     * Values:
     * - ViewFar: 265200 (1200 * 17 * 13)
     * - WidthFar: 2500, WidthNear: 150
     */
    static CameraConfig ForLoginScene()
    {
        CameraConfig config;
        config.hFov = 90.0f;
        config.nearPlane = 10.0f;
        config.farPlane = 20000.0f;
        config.terrainCullRange = 20000.0f;
        config.objectCullRange = 20000.0f;
        config.fogStart = config.farPlane * 1.0f;
        config.fogEnd = config.farPlane * 1.25f;
        return config;
    }

    /**
     * @brief Character selection scene camera configuration
     *
     * Medium frustum for character preview.
     * Shows character and surrounding environment.
     *
     * Values (Phase 5 update):
     * - FOV: 71 degrees (optimized for character viewing)
     * - Far plane: 4100 (balanced visibility)
     */
    static CameraConfig ForCharacterScene()
    {
        CameraConfig config;
        config.hFov = 40.0f; // ~30° vFOV on 4:3, matches original character scene
        config.nearPlane = 10.0f;
        config.farPlane = 4100.0f;
        config.terrainCullRange = 4100.0f;
        config.objectCullRange = 4100.0f;
        config.fogStart = config.farPlane * 1.0f;
        config.fogEnd = config.farPlane * 1.25f;
        return config;
    }
};

// Camera debug logging macro.
// The editor product has been removed; this is now always a no-op.

#define CAMERA_LOG(fmt, ...) ((void)0)

/**
 * @brief Encapsulates all camera state data
 *
 * Centralizes camera transformation data that was previously
 * scattered across global variables.
 */
class CameraState
{
  public:
    CameraState();
    ~CameraState() = default;

    // ========== Transform ==========
    vec3_t Position;    // Camera world position
    vec3_t Angle;       // Euler angles [pitch, yaw, roll] in degrees
    float Matrix[3][4]; // Cached transform matrix

    // ========== View Frustum ==========
    float ViewNear; // Near clipping plane
    float ViewFar;  // Far clipping plane
    float FOV;      // Field of view (degrees)

    // ========== Camera Behavior ==========
    float Distance;       // Current distance from target
    float DistanceTarget; // Target distance (smooth interpolation)
    short ZoomLevel;      // Camera zoom level (0-5)

    // Custom camera distance (for special terrain)
    float CustomDistance;

    // ========== Projection Cache ==========
    // These are computed by CameraProjection and cached here
    float PerspectiveX;    // Perspective factor X
    float PerspectiveY;    // Perspective factor Y
    int ScreenCenterX;     // Screen center X (pixels)
    int ScreenCenterY;     // Screen center Y (pixels)
    int ScreenCenterYFlip; // Screen center Y flipped

    /**
     * @brief Updates the cached transform matrix from position and angle
     *
     * Call this after modifying Position or Angle.
     */
    void UpdateMatrix();

    /**
     * @brief Resets camera to default state
     */
    void Reset();
};

/**
 * @brief Axis-Aligned Bounding Box for culling tests
 */
struct AABB
{
    vec3_t min;
    vec3_t max;

    AABB()
    {
        Vector(0.f, 0.f, 0.f, min);
        Vector(0.f, 0.f, 0.f, max);
    }

    AABB(const vec3_t &minimum, const vec3_t &maximum)
    {
        VectorCopy(minimum, min);
        VectorCopy(maximum, max);
    }
};

/**
 * @brief Unified frustum representation for camera culling
 *
 * Replaces the old dual frustum system (3D pyramid + 2D trapezoid).
 * Provides both 3D object culling and 2D terrain tile culling.
 */
class Frustum
{
  public:
    /**
     * @brief Frustum plane definition
     */
    struct Plane
    {
        vec3_t normal;  // Plane normal (normalized)
        float distance; // Distance from origin (D in plane equation Ax + By + Cz + D = 0)

        Plane()
        {
            Vector(0.f, 0.f, 0.f, normal);
            distance = 0.f;
        }
    };

    Frustum();
    ~Frustum() = default;

    /**
     * @brief Builds frustum from camera parameters
     *
     * @param position Camera world position
     * @param forward Camera forward vector (normalized)
     * @param up Camera up vector (normalized)
     * @param fovDegrees Vertical field of view in degrees
     * @param aspectRatio Width / Height ratio
     * @param nearDist Near clipping plane distance
     * @param farDist Far clipping plane distance
     * @param terrainCullDist Distance for terrain tile culling (defaults to farDist if not specified)
     */
    void BuildFromCamera(const vec3_t position, const vec3_t forward, const vec3_t up,
                         float fovDegrees, float aspectRatio, float nearDist, float farDist,
                         float terrainCullDist = -1.0f);

    /**
     * @brief Tests if a sphere is inside or intersecting the frustum
     *
     * @param center Sphere center in world space
     * @param radius Sphere radius
     * @return true if sphere is visible, false if completely outside
     */
    bool TestSphere(const vec3_t center, float radius) const;

    /**
     * @brief Tests if an AABB is inside or intersecting the frustum
     *
     * @param box Axis-aligned bounding box
     * @return true if box is visible, false if completely outside
     */
    bool TestAABB(const AABB &box) const;

    /**
     * @brief Tests if a point is inside the frustum
     *
     * @param point Point in world space
     * @return true if point is visible, false if outside
     */
    bool TestPoint(const vec3_t point) const;

    /**
     * @brief Gets the 8 corner vertices of the frustum
     *
     * Vertices are ordered:
     * [0-3] = Near plane corners (top-left, top-right, bottom-right, bottom-left)
     * [4-7] = Far plane corners (top-left, top-right, bottom-right, bottom-left)
     */
    const vec3_t *GetVertices() const
    {
        return m_Vertices;
    }

    /**
     * @brief Gets the 6 frustum planes (left, right, top, bottom, near, far)
     */
    const Plane *GetPlanes() const
    {
        return m_Planes;
    }

    /**
     * @brief Gets the axis-aligned bounding box that contains the frustum
     */
    const AABB &GetBoundingBox() const
    {
        return m_BoundingBox;
    }

    /**
     * @brief Cheap 2D point-in-frustum test using ground-plane projection
     *
     * Tests if a point (in tile coordinates = world/100) is inside the 2D
     * convex hull of the frustum projected to the XY ground plane.
     * Uses the same cross-product winding test as the original TestFrustrum2D.
     *
     * @param tileX X coordinate in tile space (world / 100)
     * @param tileY Y coordinate in tile space (world / 100)
     * @param range Tolerance: negative = stricter culling, 0 = exact boundary
     * @return true if point is visible (inside frustum projection)
     */
    bool TestPoint2D(float tileX, float tileY, float range) const;

    /**
     * @brief Gets the number of vertices in the 2D ground-plane convex hull
     */
    int Get2DCount() const
    {
        return m_2DCount;
    }

    /**
     * @brief Gets the X coordinates of the 2D convex hull (tile space)
     */
    const float *Get2DX() const
    {
        return m_2DX;
    }

    /**
     * @brief Gets the Y coordinates of the 2D convex hull (tile space)
     */
    const float *Get2DY() const
    {
        return m_2DY;
    }

    /**
     * @brief Overwrites the 2D ground-plane hull with custom points.
     *
     * Used by DevEditor overrides to inject a user-defined trapezoid. The points
     * must already be in tile coordinates (world / 100). Up to 12 points.
     */
    void SetCustom2DHull(const float *xs, const float *ys, int count);

  private:
    // 6 frustum planes: Left, Right, Top, Bottom, Near, Far
    Plane m_Planes[6];

    // 8 corner vertices of the frustum box
    vec3_t m_Vertices[8];

    // Axis-aligned bounding box containing the frustum
    AABB m_BoundingBox;

    // 2D ground-plane projection (convex hull of vertices projected to XY, in tile coords)
    // 8 frustum corners + up to 4 terrain-cull far corners
    static constexpr int MAX_2D_HULL_POINTS = 12;
    float m_2DX[MAX_2D_HULL_POINTS];
    float m_2DY[MAX_2D_HULL_POINTS];
    int m_2DCount;

    // Extended terrain cull far vertices (when terrainCullDist > farDist)
    vec3_t m_TerrainFarVertices[4];
    bool m_bHasTerrainExtension = false;

    // Helper methods
    void CalculateBoundingBox();
    void Calculate2DProjection();

    // BuildFromCamera helpers. Inputs are logically const but declared non-const because
    // the project's vec3_t helpers don't accept const; these methods copy to locals internally.
    void CalculateFrustumVertices(const vec3_t position, const vec3_t forward, const vec3_t up,
                                  const vec3_t right, float tanHalfFov, float aspectRatio,
                                  float nearDist, float farDist);
    void CalculateTerrainExtension(const vec3_t position, const vec3_t forward, const vec3_t up,
                                   const vec3_t right, float tanHalfFov, float aspectRatio,
                                   float farDist, float terrainCullDist);
    void CalculatePlanes(const vec3_t position, const vec3_t forward, const vec3_t nearCenter,
                         const vec3_t farCenter);
};

class CMapManager;

/**
 * @brief Base interface for all camera implementations
 *
 * Defines the contract that all cameras must fulfill. Each camera
 * computes its own position and orientation, then applies it to
 * the shared CameraState.
 *
 * Phase 1 Enhancement: Cameras now own their rendering configuration
 * and provide frustum for culling.
 */
class ICamera
{
  public:
    virtual ~ICamera() = default;

    /**
     * @brief Updates camera logic and applies to state
     * @return True if camera is locked (e.g., during cinematic)
     */
    virtual bool Update(float animationFactor) = 0;

    /**
     * @brief Resets camera to default state
     */
    virtual void Reset() = 0;

    /**
     * @brief Called when camera becomes active
     * @param previousState Previous camera state for smooth transitions
     */
    virtual void OnActivate(const CameraState &previousState, float animationFactor)
    {
    }

    /**
     * @brief Called when camera becomes inactive
     */
    virtual void OnDeactivate()
    {
    }

    /**
     * @brief Resets the user-facing view to camera defaults.
     *
     * F11 dispatches here. For Default this is the zoom-level reset; for
     * Orbital it's zoom + rotation. Other cameras (FreeFly) leave it as
     * a no-op.
     */
    virtual void ResetView()
    {
    }

    /**
     * @brief Gets human-readable camera name
     */
    virtual const char *GetName() const = 0;

    // ========== Phase 1: Configuration & Frustum Management ==========

    /**
     * @brief Gets the camera's rendering configuration
     *
     * Configuration controls FOV, near/far planes, and culling parameters.
     */
    virtual const CameraConfig &GetConfig() const = 0;

    /**
     * @brief Sets the camera's rendering configuration
     *
     * @param config New configuration to apply
     *
     * NOTE: Derived classes should rebuild frustum when config changes.
     */
    virtual void SetConfig(const CameraConfig &config) = 0;

    /**
     * @brief Gets the camera's view frustum
     *
     * Frustum is used for culling objects and terrain tiles.
     * Derived classes should update frustum in Update() method.
     */
    virtual const Frustum &GetFrustum() const = 0;

    /**
     * @brief Tests if an object should be culled (not rendered)
     *
     * @param position Object center position in world space
     * @param radius Object bounding sphere radius
     * @return true if object should be culled, false if visible
     */
    virtual bool ShouldCullObject(const vec3_t position, float radius) const = 0;

    /**
     * @brief Tests if a terrain tile should be culled (not rendered)
     *
     * @param tileX Terrain tile X coordinate (in tile space, not world space)
     * @param tileY Terrain tile Y coordinate (in tile space, not world space)
     * @return true if tile should be culled, false if visible
     */
    virtual bool ShouldCullTerrain(int tileX, int tileY) const = 0;

    /**
     * @brief Tests if an object should be culled using 2D (XY only) test
     *
     * This matches TestFrustrum2D behavior - only tests XY position against ground trapezoid
     *
     * @param x World X coordinate
     * @param y World Y coordinate
     * @param radius Object radius/tolerance
     * @return true if object should be culled, false if visible
     */
    virtual bool ShouldCullObject2D(float x, float y, float radius) const = 0;
};

/**
 * @brief Available camera modes
 */
enum class CameraMode
{
    Default, // Original third-person follow camera
    Orbital, // Spherical orbit around character (F9 toggle)
};

/**
 * @brief Returns human-readable camera mode name
 */
inline const char *CameraModeToString(CameraMode mode)
{
    switch (mode)
    {
    case CameraMode::Default:
        return "Default";
    case CameraMode::Orbital:
        return "Orbital";
    default:
        return "Unknown";
    }
}

/**
 * @brief Cycles to next camera mode
 * @param current Current mode
 * @return Next mode in sequence
 */
inline CameraMode GetNextCameraMode(CameraMode current)
{
    switch (current)
    {
    case CameraMode::Default:
        return CameraMode::Orbital;
    case CameraMode::Orbital:
        return CameraMode::Default;
    default:
        return CameraMode::Default;
    }
}

class SessionKeeper;
class OrbitalCamera;

/**
 * @brief Manages camera modes and switching
 *
 * Per-session owner of all camera instances and mode switching.
 */
class CameraManager : protected SessionLegacyCalls
{
  public:
    explicit CameraManager(SessionKeeper &keeper) noexcept;
    ~CameraManager();

    /**
     * @brief Initializes camera system
     */
    void Initialize(float animationFactor);

    /**
     * @brief Shuts down camera system
     */
    void Shutdown();

    /**
     * @brief Updates active camera
     * @return True if camera is locked
     */
    bool Update(float animationFactor);

    /**
     * @brief Sets active camera mode
     * @param mode Target camera mode
     * @return True if mode was changed
     */
    bool SetCameraMode(CameraMode mode, float animationFactor);

    /**
     * @brief Cycles to next camera mode (F9 key)
     */
    void CycleToNextMode(float animationFactor);

    /**
     * @brief Gets current camera mode
     */
    CameraMode GetCurrentMode() const
    {
        return m_CurrentMode;
    }

    /**
     * @brief Gets active camera instance
     */
    ICamera *GetActiveCamera() const
    {
        return m_pActiveCamera;
    }
    CameraManager &CameraManager_Instance();
    OrbitalCamera *GetOrbitalCameraInstance();
    void GetOrbitalCameraAngles(float *outYaw, float *outPitch);
    void GetActiveCameraConfig(float *outFOV, float *outNearPlane, float *outFarPlane,
                               float *outTerrainCullRange);

    /**
     * @brief Whether camera zoom (and orbital rotation) is locked.
     *
     * F10 toggles this flag. It defaults to true so the camera doesn't
     * react to the wheel until the player explicitly unlocks it.
     */
    bool IsZoomLocked() const
    {
        return m_ZoomLocked;
    }

    /**
     * @brief Flips the zoom-lock flag (F10 key).
     */
    void ToggleZoomLock()
    {
        m_ZoomLocked = !m_ZoomLocked;
    }

    /**
     * @brief Resets the active camera's user-facing view (F11 key).
     */
    void ResetActiveView()
    {
        if (m_pActiveCamera)
            m_pActiveCamera->ResetView();
    }

  private:
    CameraManager(const CameraManager &) = delete;
    CameraManager &operator=(const CameraManager &) = delete;

    SessionKeeper &sessionKeeper_;
    CameraState &g_Camera;
    CameraMode m_CurrentMode;
    ICamera *m_pActiveCamera;
    bool m_ZoomLocked = true; // F10 toggles; default = locked
    // Camera instances
    std::unique_ptr<ICamera> m_pDefaultCamera;
    std::unique_ptr<ICamera> m_pOrbitalCamera;

    void TransitionToCamera(ICamera *pNewCamera, float animationFactor);
};

class SessionKeeper;
class CMapManager;

class CCameraMove final : protected SessionLegacyCalls
{
    friend class CharacterRetirementTestPeer;
    using WAYPOINT = WorldCameraData::Waypoint;
    using t_WayPointList = std::vector<WAYPOINT>;
    t_WayPointList m_listWayPoint;
    CMapManager &mapManager_;

    float m_CameraStartPos[3];
    float m_fCameraStartDistanceLevel;
    double m_iDelayCount;

    DWORD m_dwCameraWalkState;
    float m_CurrentCameraPos[3];
    float m_fCurrentDistanceLevel;

    DWORD m_dwCurrentIndex;
    int m_iSelectedTile;

    friend class SessionLegacyCalls;

    void Init();
    void RenderWayPointLine();
    void UpdateTourDistance(const WAYPOINT &origin, const WAYPOINT &target, float distanceToOrigin,
                            float distanceToTarget);
    void PrepareTourMotion(const WAYPOINT &origin, const WAYPOINT &target);
    void AdvanceTourMotion(double frames);
    void FinishTourSegment();
    void ApplyLoginSceneOffset(float &x, float &y, float &z);

  public:
    enum
    {
        CAMERAWALK_STATE_READY = 0,
        CAMERAWALK_STATE_MOVE,
        CAMERAWALK_STATE_DONE,
    };
    ~CCameraMove();

    void InstallCameraWalkScript(WorldCameraData &&data) noexcept;
    void UnLoadCameraWalkScript();
    bool SaveCameraWalkScript(const std::wstring &filename);

    void AddWayPoint(int iGridX, int iGridY, float fCameraMoveAccel, float fCameraDistanceLevel,
                     int iDelay);
    void RemoveWayPoint(int iGridX, int iGridY);

    void SetCameraMoveAccel(int iTileIndex, float fCameraMoveAccel);
    void SetCameraDistanceLevel(int iTileIndex, float fCameraDistanceLevel);
    void SetDelay(int iTileIndex, int iDelay);
    float GetCameraMoveAccel(int iTileIndex);
    float GetCameraDistanceLevel(int iTileIndex);
    int GetDelay(int iTileIndex);

    bool IsCameraMove() const;
    void UpdateWayPoint(float animationFactor);
    void GetCurrentCameraPos(float CameraPos[3]);
    float GetCurrentCameraDistanceLevel() const;

    void PlayCameraWalk(float StartPos[3], float fStartDistanceLevel);
    void StopCameraWalk(bool bDone);
    void UpdateCameraStartPos(float StartPos[3]);

    void SetCameraWalkState(DWORD dwCameraWalkState);
    DWORD GetCameraWalkState() const;

    void RenderWayPoint();
    void SetSelectedTile(int iTileIndex);
    DWORD GetSelectedTile() const;

  public:
    explicit CCameraMove(SessionKeeper &keeper);

  private:
    WAYPOINT *GetWayPointByIndex(std::size_t index);
    const WAYPOINT *GetWayPointByIndex(std::size_t index) const;
    WAYPOINT *FindWayPointByTile(int tileIndex);
    const WAYPOINT *FindWayPointByTile(int tileIndex) const;

    BOOL m_bTourMode;
    BOOL m_bTourPause;
    float m_fForceSpeed;
    float m_fTourCameraAngle;
    float m_fTargetTourCameraAngle;
    float m_vTourCameraPos[3];
    float m_fCameraAngle;
    float m_fFrustumAngle;
    double tourMotionFrames_ = 0.0;
    double tourMotionElapsed_ = 0.0;
    std::array<float, 2> tourRouteStart_{};
    std::array<float, 2> tourRouteVelocity_{};
    float tourAngleStart_ = 0.f;
    bool tourArrives_ = false;

  public:
    BOOL SetTourMode(BOOL bFlag, BOOL bRandomStart = FALSE, int _index = 0);
    BOOL IsTourMode()
    {
        return m_bTourMode;
    }
    BOOL IsTourPaused()
    {
        return m_bTourPause;
    }
    void PauseTour(BOOL bFlag);
    void ForwardTour(float fSpeed);
    void BackwardTour(float fSpeed);
    void UpdateTourWayPoint(float animationFactor);
    void SetAngleFrustum(float _Value);
    const float GetAngleFrustum() const
    {
        return m_fCameraAngle;
    };
    void SetFrustumAngle(float _Value);
    float GetFrustumAngle();

    float GetCameraAngle()
    {
        return m_fTourCameraAngle;
    }

  private:
};

// Forward declaration
class CameraState;
class SessionKeeper;
class LegacyRenderFacade;

/**
 * @brief Camera projection and viewport utilities
 *
 * Handles projection matrix setup, screen-space transformations,
 * and viewport management. Extracted from ZzzOpenglUtil.cpp.
 */
class CameraProjection
{
  public:
    explicit CameraProjection(SessionKeeper &keeper) noexcept;
    /**
     * @brief Sets up OpenGL perspective projection
     *
     * Replaces gluPerspective2(). Updates both OpenGL state and
     * caches perspective factors in CameraState.
     *
     * @param state Camera state to update with cached factors
     * @param fov Field of view (degrees)
     * @param aspect Aspect ratio (width/height)
     * @param zNear Near clipping plane
     * @param zFar Far clipping plane
     */
    void PrepareInteractionProjection(CameraState &state, int width, int height);
    void SetupPerspective(CameraState &state, float fov, float aspect, float zNear, float zFar);

    /**
     * @brief Sets OpenGL viewport with Y-axis flip
     *
     * Replaces glViewport2(). Takes pixel coordinates directly (not reference coords).
     *
     * @param x Viewport X offset (pixels)
     * @param y Viewport Y offset (pixels)
     * @param width Viewport width (pixels)
     * @param height Viewport height (pixels)
     */
    void SetViewport(int x, int y, int width, int height);

    /**
     * @brief Converts screen coordinates to world ray direction
     *
     * Replaces CreateScreenVector().
     *
     * @param state Camera state with cached perspective factors
     * @param sx Screen X coordinate (in 640×480 reference coordinates)
     * @param sy Screen Y coordinate (in 640×480 reference coordinates)
     * @param outTarget Output world direction vector
     * @param bFixView Use camera view far (true) or item view far (false)
     */
    void ScreenToWorldRay(const CameraState &state, int sx, int sy, vec3_t outTarget,
                          bool bFixView = true);

    /**
     * @brief Projects world position to screen coordinates (reference)
     *
     * Replaces Projection(). Returns coordinates in 640×480 reference space.
     *
     * @param state Camera state with cached transform and perspective
     * @param worldPos World position
     * @param outX Output screen X (in 640×480 reference coordinates)
     * @param outY Output screen Y (in 640×480 reference coordinates)
     */
    void WorldToScreen(const CameraState &state, const vec3_t worldPos, int *outX, int *outY);

    /**
     * @brief Transforms position relative to camera (pixel coordinates)
     *
     * Replaces TransformPosition(). Returns coordinates in actual pixel space.
     *
     * @param state Camera state
     * @param position World position
     * @param outWorldPosition Output camera-relative position
     * @param outX Output screen X (in actual pixels)
     * @param outY Output screen Y (in actual pixels)
     */
    void TransformPosition(const CameraState &state, const vec3_t position, vec3_t outWorldPosition,
                           int *outX, int *outY);

    /**
     * @brief Reads OpenGL modelview matrix
     *
     * Replaces GetOpenGLMatrix().
     *
     * @param outMatrix Output 3×4 matrix
     */
    void GetModelViewMatrix(float outMatrix[3][4]);

  private:
    LegacyRenderFacade *RecordingFacade() const noexcept;

    SessionKeeper &keeper_;
    int &OpenglWindowX;
    int &OpenglWindowY;
    int &OpenglWindowWidth;
    int &OpenglWindowHeight;
    unsigned int &WindowWidth;
    unsigned int &WindowHeight;
    vec3_t &MousePosition;
    int viewportWidth_ = 0;
    int viewportHeight_ = 0;
};

// Minimal 2D convex hull utility shared by the camera frustum (Frustum.cpp)
// and the terrain culling hull (ZzzLodTerrain.cpp). Both build a small (<= a
// few dozen points) hull each frame from frustum corners, so an O(n^2)
// insertion sort plus Andrew's monotone chain is more than fast enough.

struct Point2D
{
    float x, y;
};

// z-component of the 3D cross product (b - a) x (c - a). Sign tells you the
// turn direction at b: positive = left turn (CCW), negative = right turn (CW).
inline float Cross2D(const Point2D &a, const Point2D &b, const Point2D &c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// In-place insertion sort by X then Y. Stable; intended for small `count`.
inline void SortPoints2D(Point2D *pts, int count)
{
    for (int i = 1; i < count; i++)
    {
        Point2D key = pts[i];
        int j = i - 1;
        while (j >= 0 && (pts[j].x > key.x || (pts[j].x == key.x && pts[j].y > key.y)))
        {
            pts[j + 1] = pts[j];
            j--;
        }
        pts[j + 1] = key;
    }
}

// Andrew's monotone chain. `sortedPts` MUST already be sorted via SortPoints2D.
// Writes a CCW-ordered hull to `outHull` (capacity `hullCapacity`) and returns
// the number of hull points written. Caller is responsible for re-ordering to
// CW if their downstream test (e.g. TestFrustrum2D) expects that winding.
inline int ConvexHullCCW(const Point2D *sortedPts, int numPts, Point2D *outHull, int hullCapacity)
{
    int k = 0;

    // Lower hull
    for (int i = 0; i < numPts; i++)
    {
        while (k >= 2 && Cross2D(outHull[k - 2], outHull[k - 1], sortedPts[i]) <= 0.f)
            k--;
        if (k < hullCapacity)
            outHull[k++] = sortedPts[i];
    }

    // Upper hull
    int lowerSize = k + 1;
    for (int i = numPts - 2; i >= 0; i--)
    {
        while (k >= lowerSize && Cross2D(outHull[k - 2], outHull[k - 1], sortedPts[i]) <= 0.f)
            k--;
        if (k < hullCapacity)
            outHull[k++] = sortedPts[i];
    }
    k--; // Remove duplicate last point
    return k;
}

class CameraManager;
class CameraProjection;
class CCameraMove;
class CDirection;
class CUIRenderText;
class SessionKeeper;

/**
 * @brief Default legacy third-person follow camera
 *
 * This is the original camera implementation extracted from CameraUtility.cpp.
 * It handles all the legacy game-specific behavior including terrain flags,
 * tour mode, direction system, scene-specific positioning, etc.
 *
 * This remains the DEFAULT camera mode. In Phase 2, we'll add OrbitalCamera
 * and FreeFlyCameraEditor as alternatives.
 */
class DefaultCamera : public ICamera, protected SessionLegacyCalls
{
  public:
    explicit DefaultCamera(SessionKeeper &keeper);
    ~DefaultCamera() override = default;

    // ICamera interface
    bool Update(float animationFactor) override;
    void Reset() override;
    void OnActivate(const CameraState &previousState, float animationFactor) override;
    void OnDeactivate() override;
    void ResetView() override;
    const char *GetName() const override
    {
        return "Default";
    }

    // Phase 5: Scene-specific reset
    void ResetForScene(EGameScene scene);

    // Current mount camera offset (smoothly lerped). Used by OrbitalCamera
    // to apply the same lift without duplicating the lerp logic.
    float GetMountCameraOffset() const
    {
        return m_CurrentMountOffset;
    }

    // Snap m_CurrentMountOffset to the current target (skip the lerp).
    // Call on camera activation so the internal state matches immediately.
    void SyncMountOffset();

    // Phase 1: Configuration & Frustum Management
    const CameraConfig &GetConfig() const override
    {
        return m_Config;
    }
    void SetConfig(const CameraConfig &config) override;
    const Frustum &GetFrustum() const override
    {
        return m_Frustum;
    }

    bool ShouldCullObject(const vec3_t position, float radius) const override
    {
        return !m_Frustum.TestSphere(position, radius);
    }

    bool ShouldCullTerrain(int tileX, int tileY) const override
    {
        // Use cheap 2D ground-plane projection test for terrain
        // (same algorithm as original TestFrustrum2D, ~4 cross-products vs 6-plane sphere test)
        return !m_Frustum.TestPoint2D(tileX + 0.5f, tileY + 0.5f, -40.0f);
    }

    bool ShouldCullObject2D(float x, float y, float radius) const override
    {
        // Use 3D frustum sphere test instead of 2D ground projection
        vec3_t position;
        position[0] = x;
        position[1] = y;
        position[2] = 0.0f; // Objects on ground
        return !m_Frustum.TestSphere(position, radius);
    }

  private:
    CameraState &m_State;
    CameraState &g_Camera;
    CMapManager &gMapManager;
    CCameraMove &cameraMove_;
    CameraManager &cameraManager_;
    CameraProjection &cameraProjection_;
    CDirection &g_Direction;
    int &MouseWheel;
    unsigned int &WindowWidth;
    unsigned int &WindowHeight;

    // Phase 1: Configuration and frustum
    CameraConfig m_Config;
    Frustum m_Frustum;

    // Phase 5: Scene transition tracking and Hero validity
    int m_LastSceneFlag = -1;
    bool m_bJustActivated = false;   // Skip first frame update to preserve inherited position
    int m_FramesSinceActivation = 0; // Count frames since activation to disable smoothing
    bool IsHeroValid() const;

    // Player-controlled zoom level for the third-person ladder.
    // Persists across frames (the per-frame g_shCameraLevel is reseeded from
    // this member in MAIN_SCENE only). Range 0..7 maps to distances
    // 1000..1700 in 100-unit steps; default level 3 = 1300. F11 (handled in
    // CameraUtility) calls ResetView() which snaps it back to the default;
    // cutscene direction mode temporarily overrides g_shCameraLevel via
    // CDirection::GetCameraPosition().
    int m_PlayerZoomLevel = 3;
    void HandleWheelZoom();

    // These are direct copies of the static functions from CameraUtility.cpp
    // We're NOT refactoring them yet - just moving them as-is
    void CalculateCameraViewFar();
    void UpdateMountOffset(float animationFactor);
    void CalculateCameraPosition(float animationFactor);
    void SetCameraAngle();
    void UpdateCustomCameraDistance(float animationFactor);
    void UpdateCameraDistance(float animationFactor);
    void SetCameraFOV();
    void UpdateFrustum();            // Phase 1: Rebuild frustum from current state
    bool NeedsFrustumUpdate() const; // Phase 5: Check if frustum needs rebuild

    // ResetForScene helpers: each loads the scene-specific config and initial transform.
    void ApplyConfigToState();
    void InvalidateFrustumCache();
    void InitCharacterScene();
    void InitMainScene();
    void InitLoginScene();

    // Mount height offset — smooth lerp when mounting/dismounting
    float GetTargetMountOffset() const;
    float m_CurrentMountOffset = 0.0f;
    int m_LastMountType = -1;

#ifdef ENABLE_EDIT2
    void HandleEditorMode(float animationFactor);
#endif

    // Phase 5: Cache last frustum state + editor config to avoid unnecessary rebuilds
    mutable struct FrustumCache
    {
        vec3_t Position = {};
        vec3_t Angle = {};
        float ViewFar = 0.0f;
        float AspectRatio = 0.0f;
        float EditorFOV = 0.0f;
        float EditorFarPlane = 0.0f;
        float EditorNearPlane = 0.0f;
        float EditorTerrainCullRange = 0.0f;
    } m_FrustumCache;
};

class CameraManager;
class CameraProjection;
class SessionKeeper;
class CHARACTER;
struct SessionCharacterPopulationStorage;

/**
 * @brief Orbital camera - spherical coordinates around character
 *
 * Features:
 * - Middle mouse drag to rotate (yaw + pitch)
 * - Mouse wheel to zoom (radius)
 * - Maintains character focus
 * - Available in all builds
 */
class OrbitalCamera : public ICamera, protected SessionLegacyCalls
{
  public:
    explicit OrbitalCamera(SessionKeeper &keeper);
    ~OrbitalCamera() override = default;

    // ICamera interface
    bool Update(float animationFactor) override;
    void Reset() override;
    void OnActivate(const CameraState &previousState, float animationFactor) override;
    void OnDeactivate() override;
    void ResetView() override;
    const char *GetName() const override
    {
        return "Orbital";
    }

    // Phase 5: Scene-specific reset
    void ResetForScene(EGameScene scene);

    // Public accessors for UI
    float GetRadius() const
    {
        return m_Radius;
    }
    float GetTotalYaw() const
    {
        return m_BaseYaw + m_DeltaYaw;
    }
    float GetTotalPitch() const
    {
        return m_BasePitch + m_DeltaPitch;
    }

    // Phase 5: Public accessor for debug display
    void GetTargetPosition(vec3_t outTarget) const;

    // Phase 1: Configuration & Frustum Management
    const CameraConfig &GetConfig() const override
    {
        return m_Config;
    }
    void SetConfig(const CameraConfig &config) override;
    const Frustum &GetFrustum() const override
    {
        return m_Frustum;
    }

    bool ShouldCullObject(const vec3_t position, float radius) const override
    {
        return !m_Frustum.TestSphere(position, radius);
    }

    bool ShouldCullTerrain(int tileX, int tileY) const override
    {
        // Use cheap 2D ground-plane projection test for terrain
        // (same algorithm as original TestFrustrum2D, ~4 cross-products vs 6-plane sphere test)
        return !m_Frustum.TestPoint2D(tileX + 0.5f, tileY + 0.5f, -40.0f);
    }

    bool ShouldCullObject2D(float x, float y, float radius) const override
    {
        // Use 3D frustum sphere test instead of 2D ground projection
        vec3_t position;
        position[0] = x;
        position[1] = y;
        position[2] = 0.0f; // Objects on ground
        return !m_Frustum.TestSphere(position, radius);
    }

  private:
    CameraState &m_State;
    CameraState &g_Camera;
    CameraManager &cameraManager_;
    CameraProjection &cameraProjection_;
    CHARACTER *&Hero;
    SessionCharacterPopulationStorage &CharactersClient;
    int &MouseWheel;
    bool &MouseMButton;
    bool &MouseMButtonPush;
    int &MouseX;
    int &MouseY;
    EGameScene &SceneFlag;
    unsigned int &WindowWidth;
    unsigned int &WindowHeight;
    int &m_CameraZoom;
    std::unique_ptr<DefaultCamera> m_pDefaultCamera; // Internal default camera for base calculation

    // Phase 1: Configuration and frustum
    CameraConfig m_Config;
    Frustum m_Frustum;

    // Orbital parameters
    vec3_t m_InitialCameraOffset; // Saved offset from character on first frame
    bool m_bInitialOffsetSet;     // Has initial offset been captured?
    vec3_t m_Target;              // Orbit center (character position)
    float m_BaseYaw;              // Initial yaw when activated
    float m_BasePitch;            // Initial pitch when activated
    float m_DeltaYaw;             // User rotation delta from base
    float m_DeltaPitch;           // User pitch delta from base
    float m_Radius;               // Distance from target

    // Phase 5: Scene transition tracking
    int m_LastSceneFlag;   // Track scene changes to reset target
    bool m_bJustActivated; // Skip DefaultCamera update on first frame after activation

    // Mount offset that was baked into the inherited position at activation time.
    // We subtract this so only the DELTA is applied, avoiding double-offset.
    float m_ActivationMountOffset = 0.0f;

    // Constraints
    static constexpr float MIN_PITCH = -80.0f; // Look down limit
    static constexpr float MAX_PITCH = 80.0f;  // Look up limit
    // Radius values are absolute camera-to-Hero distances in world units.
    // ComputeCameraTransform normalizes the captured offset direction and
    // scales it to m_Radius, so what you set here is what you get on screen.
    // DEFAULT_RADIUS is also the "neutral" radius for UpdateConfigForView's
    // far-plane / zoom-lift scaling — at this radius, scale = 1.
    // Default cam at level 3 (1300 body distance + 1150 Z-lift) gives an
    // actual camera-to-Hero distance of ~sqrt(919² + 919² + 1150²) ≈ 1735.
    // DEFAULT matches that so orbital starts at exactly the Default cam's
    // position. MIN/MAX bracket how far the player can move from there.
    static constexpr float MIN_RADIUS = 600.0f;
    static constexpr float MAX_RADIUS = 3000.0f;
    static constexpr float DEFAULT_RADIUS = 1735.0f;

    // Input state (middle-mouse drag tracking)
    struct InputState
    {
        bool Rotating = false; // Middle mouse button held?
        int LastMouseX = 0;
        int LastMouseY = 0;
        float LastEffectivePitch = 0.0f; // Last effective pitch applied (after constraints)
    } m_Input;

    // Helper methods
    void HandleInput();
    void UpdateTarget();
    void ComputeCameraTransform();
    void UpdateFrustum();       // Phase 1: Rebuild frustum from current state
    void UpdateConfigForView(); // Phase 1: Adjust config based on zoom level and pitch

    // OnActivate / ResetForScene helpers
    void LoadConfigForScene(EGameScene scene);
    void ApplyConfigToState();
    void CalculateOrbitOriginForStaticScene(EGameScene scene, const CameraState &previousState);
    void CalculateLookAtPoint(EGameScene scene, vec3_t outLookAt) const;
    void InitializeOrbitalFromCurrentState(const vec3_t lookAtPoint,
                                           const CameraState &previousState);
    void SyncStateToGlobalCamera();
    void PersistZoom();

    // Phase 5: Hero validity check (private)
    bool IsHeroValid() const;
};

struct LoginCameraState
{
    int walkCut = 0;
    int currentCount = -1;
    int currentNumber = 0;
    int currentWalkType = 0;
    float currentPosition[3] = {0.f, 0.f, 0.f};
    float currentAngle[3] = {0.f, 0.f, 0.f};
    float currentWalkDelta[6] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};

    static constexpr float WALK_PATHS[6][6] = {
        {0.f, -1000.f, 500.f, -80.f, 0.f, 0.f}, {0.f, -1100.f, 500.f, -80.f, 0.f, 0.f},
        {0.f, -1100.f, 500.f, -80.f, 0.f, 0.f}, {0.f, -1100.f, 500.f, -80.f, 0.f, 0.f},
        {0.f, -1100.f, 500.f, -80.f, 0.f, 0.f}, {200.f, -800.f, 250.f, -87.f, 0.f, -10.f},
    };

    void Reset() noexcept
    {
        currentCount = -1;
        walkCut = 0;
        currentNumber = 0;
        currentWalkType = 0;
    }
};
