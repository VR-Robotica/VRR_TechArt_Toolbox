using UnityEngine;

namespace VRR.TechArtTools.Lightmapping
{
    /// <summary>
    /// Object used to store lightmap data settings to be applied when scene lighting asset is not available
    /// - eg. when wanting to keep lightmap information on prefabs that are not locked to a scene
    /// </summary>
    public class LightmapDataStorage : MonoBehaviour 
    {
        [System.Serializable]
        public struct RendererInfo 
        {
            public Renderer renderer;
            public int LightmapIndex;
            public Vector4 ScaleOffset;
        }

        [SerializeField] private RendererInfo _rendererInfo;

        #region GETTERS
        public void GetInfo() 
        {
            _rendererInfo.renderer = GetComponent<Renderer>();

            if (_rendererInfo.renderer) 
            {
                _rendererInfo.LightmapIndex = _rendererInfo.renderer.lightmapIndex;
                _rendererInfo.ScaleOffset = _rendererInfo.renderer.lightmapScaleOffset;
            }
        }

        public RendererInfo GetRendererInfo() 
        {
            GetInfo();

            return _rendererInfo;
        }

        public Renderer GetRenderer() 
        {
            return _rendererInfo.renderer;
        }

        public int GetLightmapIndex() 
        {
            return _rendererInfo.LightmapIndex;
        }

        public Vector4 GetOffsetScale() 
        {
            return _rendererInfo.ScaleOffset;
        }
        #endregion

        #region SETTERS
        public void SetRenderer(Renderer renderer) 
        {
            _rendererInfo.renderer = renderer;
        }

        public void SetScaleOffset(Vector4 scaleOffset) 
        {
            _rendererInfo.ScaleOffset = scaleOffset;
        }

        public void SetLightmapIndex(int index) 
        {
            _rendererInfo.LightmapIndex = index;
        }
        #endregion
    }
}