using UnityEngine;

using VRR.TechArtTools.Utilities;


namespace VRR.TechArtTools.Lightmapping
{
    [RequireComponent(typeof(LightmapManager))]
    public class LightmapSwapper : TextureSwapper
    {
        private LightmapManager _manager;

        #region CORE FUNCTIONS
        protected override void Swap(Texture2D[] textureSet)
        {
            if (textureSet.Length == 0) { return; }

            if(_manager == null)
            {
                _manager = this.gameObject.GetComponent<LightmapManager>();
            }

            if (_manager != null)
            {
                _manager.SetLightmapTextures(textureSet);
                _manager.Apply();
            }
            else
            {
                Debug.Log("<color=red>Lightmap Manager NOT Found!</color>");
            }
        }
        #endregion
    }
}