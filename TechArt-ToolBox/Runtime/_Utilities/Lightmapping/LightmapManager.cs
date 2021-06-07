using System.Collections.Generic;
using UnityEngine;

namespace VRR.TechArtTools.Lightmapping
{
    [ExecuteInEditMode]
    public class LightmapManager : MonoBehaviour 
    {
        [SerializeField] private Renderer[] _renderers;
        [SerializeField] private Texture2D[] _lightmapTextures;
        [SerializeField] Dictionary<Renderer, Vector4> scaleOffset;


        #region MONOBEHAVIORS
        /*
        private void Awake() 
        {
            RegisterListeners();
        }
        */
        private void Start() 
        {
            ApplyLightMapData();
            ApplyLightMapTextures();
        }

        private void OnEnable() 
        {
            ApplyLightMapData();
            ApplyLightMapTextures();
        }

        private void OnDisable() 
        {
            //DeregisterListeners();
        }
        
        private void OnDestroy() 
        {
            // DeregisterListeners();
            LightmapSettings.lightmaps = null;
        }
        #endregion
        /*
        #region EVENT REGISTER
        private void RegisterListeners() 
        {
            Generic_EventBus.StartListening(CustomEventTypes.BUTTON_Y, Apply);
        }

        private void DeregisterListeners() 
        {
            if (Generic_EventBus.Instance != null) 
        {
                Generic_EventBus.StopListening(CustomEventTypes.BUTTON_Y, Apply);
            }
        }
        #endregion
        */
        #region GETTERS
        public void GetRenderers() 
        {
            _renderers = this.gameObject.GetComponentsInChildren<Renderer>();
        }

        public void GetLightmapTextures() 
        {
            LightmapData[] lightmapData = LightmapSettings.lightmaps;

            _lightmapTextures = new Texture2D[lightmapData.Length];

            for (int i = 0; i < _lightmapTextures.Length; i++) 
            {
                _lightmapTextures[i] = lightmapData[i].lightmapColor;
            }
        }


        #endregion

        #region SETTERS

        public void SetLightmapTextures(Texture2D[] textureSet)
        {
            if (_lightmapTextures.Length == textureSet.Length)
            {
                _lightmapTextures = textureSet;
            }
            else
            {
                Debug.Log("<color=red>Lightmap Set length mismatched!</color>");
            }
        }

        public void SetLightMapDataValues() 
        {
            Renderer[] renderers = this.gameObject.GetComponentsInChildren<Renderer>();

            foreach (Renderer renderer in renderers) 
            {
                LightmapDataStorage data = renderer.gameObject.GetComponent<LightmapDataStorage>();

                if (data == null) 
                {
                    data = renderer.gameObject.AddComponent<LightmapDataStorage>();
                }

                data.SetRenderer(renderer);
                data.SetLightmapIndex(renderer.lightmapIndex);
                data.SetScaleOffset(renderer.lightmapScaleOffset);
            }
        }

        public void ApplyLightMapData() 
        {
            if (_renderers.Length <= 0) 
            {
                return;
            }

            LightmapDataStorage lightMapData;

            for (int i = 0; i < _renderers.Length; i++) 
            {
                if (_renderers[i]) 
                {
                    lightMapData = _renderers[i].gameObject.GetComponent<LightmapDataStorage>();

                    if (lightMapData == null) { break; }

                    if (_renderers[i] != null)
                    {
                        int index = lightMapData.GetLightmapIndex();
                        _renderers[i].lightmapIndex = index;

                        // NOTE: 
                        // During Gameplay, Static Objects can not have their scale and offsets changed, 
                        // as they are dynamically combined into a single static mesh
                        //
                        // So, we check if our App is NOT playing or the object is NOT static, 
                        // then we can change the scale and offset - ie while setting up in the Editor.
                        if (!Application.isPlaying || !_renderers[i].gameObject.isStatic)
                        {                            
                            Vector4 scaleOffset = lightMapData.GetOffsetScale();
                            _renderers[i].lightmapScaleOffset = scaleOffset;
                        }
                    } 
                    else 
                    {
                        Debug.Log(_renderers[i].name + " Is Disabled");
                    }
                }
            }
        }
        public void ApplyLightMapTextures() 
        {
            if (_lightmapTextures == null || _lightmapTextures.Length <= 0) 
            {
                Debug.Log("<color=cyan>" + this.name + "</color> No Lightmap Textures in Library");
                return;
            }

            LightmapData[] lightmapData = new LightmapData[_lightmapTextures.Length];

            for (int i = 0; i < _lightmapTextures.Length; i++) 
            {
                lightmapData[i] = new LightmapData();
                lightmapData[i].lightmapColor = _lightmapTextures[i];
            }

            LightmapSettings.lightmapsMode = LightmapsMode.NonDirectional;

            LightmapSettings.lightmaps = lightmapData;
        }
        #endregion

        public void Apply() 
        {
            ClearData();

            ApplyLightMapData();
            ApplyLightMapTextures();
        }

        public void ClearData() 
        {
            LightmapData[] lightmapData = new LightmapData[_lightmapTextures.Length];

            for (int i = 0; i < _lightmapTextures.Length; i++) 
            {
                lightmapData[i] = new LightmapData();
                lightmapData[i].lightmapColor = null;
            }

            //LightmapSettings.lightmapsMode = LightmapsMode.NonDirectional;

            //LightmapSettings.lightmaps = lightmapData;
        }

        public void ClearAllLightmapDataContainers() 
        {
            Renderer[] renderers = this.gameObject.GetComponentsInChildren<Renderer>();

            foreach (Renderer renderer in renderers) 
            {
                LightmapDataStorage data = renderer.gameObject.GetComponent<LightmapDataStorage>();

                if (data != null) 
                {
                    DestroyImmediate(data);
                }
            }

            _renderers = null;
            _lightmapTextures = null;
        }

        public void SaveLightMapDataValues() 
        {
            scaleOffset = new Dictionary<Renderer, Vector4>();

            Renderer[] renderers = this.gameObject.GetComponentsInChildren<Renderer>();

            foreach (Renderer renderer in renderers) 
            {
                scaleOffset.Add(renderer, renderer.lightmapScaleOffset);
            }
        }

        private void AddLightmapData(Renderer[] renderers) 
        {
            foreach (Renderer renderer in renderers) 
            {
                renderer.gameObject.AddComponent<LightmapDataStorage>();
            }
        }
    }
}