using System.Collections;
using System.Collections.Generic;
using UnityEngine;

namespace VRR.TechArtTools.BakingTools
{
    public enum BakedTextureResolution { High, Medium, Low, UltraLow };

    [System.Serializable]
    public class TextureBaker : MonoBehaviour
    {
        public List<BakeGroup>  BakeGroups;
        private Texture2D[]     out_textures;
        public LayerMask        cameraLayerMask;

        // camera render texture
        private RenderTexture   in_renderTexture;
        private Camera          _rtCamera;
        private int             _rtResolution;
        private bool            _queued;

        public string Attribute_DiffusePower    = "_Diffuse_Power";
        public string Attribute_RimPower        = "_Rim_Power";
        public string Attribute_RimBrightness   = "_Rim_Brightness";

        private float desktopDelayTime = 10.0f;
        private float mobileDelayTime  = 60.0f;



        #region MONONBEHAVIOR
        private void Awake()
        {
            CleanStart();
        }

        private void CleanStart()
        {
            _rtCamera = null;

            Camera destroythis = this.gameObject.GetComponent<Camera>();

            if (destroythis != null)
            {
                Destroy(destroythis);
            }

            BakeGroups = new List<BakeGroup>();
        }

        private void OnEnable()
        {
            SetDefaultTextures();
        }

        private void OnDestroy()
        {
            // clear "out" textures
            foreach (Texture2D out_texture in out_textures)
            {
                // Explicitly mark the texture for destruction.
                Texture2D.Destroy(out_texture);
            }
            out_textures = null;
            CleanStart();
            SetDefaultTextures();
        }
        #endregion

        #region SETTERS
        private void SetDefaultTextures()
        {
            for(int i = 0; i < BakeGroups.Count; i++)
            {
                BakeGroups[i].Set_DefaultTexture();
            }
        }
        #endregion

        #region BAKING FUNCTIONS
        public void BakeGroup(int groupIndex)
        {
            // Step 1: make sure our index is valid
            if (groupIndex < BakeGroups.Count && BakeGroups[groupIndex] != null)
            {
                if (BakeGroups[groupIndex].TargetMaterial == null)
                {
                    Debug.Log("<color=red>*ERROR:</color> - <color=cyan>" + BakeGroups[groupIndex].name + "</color> Target Material <color=red>NOT SET</color>!");
                    return;
                }


                // Step 1a: activate the group so it can be rendered
                BakeGroups[groupIndex].gameObject.SetActive(true);

                // Step 1b: check for our source materials
                if (BakeGroups[groupIndex].SourceMaterials == null)
                {
                    // if not, get them...
                    BakeGroups[groupIndex].Get_Materials();
                }

                // Step 1c: adjust brightness (disable any lighting effects on the material)
                BakeGroups[groupIndex].Set_BaseBrightness();
            }
            else
            {
                Debug.Log("<color=cyan>" + this.gameObject.name + "</color> tried to bake a group index [<color=orange>" + groupIndex + "</color>] that does not exist!");
                return;
            }

            // Step 2: create the Camera Render Texture
            CreateRenderTexture(BakeGroups[groupIndex].Resolution);

            // Step 3: Create/Enable our camera
            CreateRTCamera().enabled = true;

            // Step 4: set render texture to ACTIVE
            RenderTexture.active = in_renderTexture;

            //  Step 5: set camera's target texture to the currently active render texture
            _rtCamera.targetTexture = RenderTexture.active;
            
            // Step 6: render the render texture
            _rtCamera.Render();

            // Step 7: Clean up old "out texture" 
            if (out_textures != null && out_textures.Length > 0 && out_textures[groupIndex] != null)
            {
                // this must be explicitly destroyed or else it sits in memory
                #if UNITY_EDITOR
                Texture2D.DestroyImmediate(out_textures[groupIndex]);
                #elif UNITY_ENGINE
                Texture2D.Destroy(out_textures[groupIndex]);
                #endif

                out_textures[groupIndex] = null;
            }

            // Step 8: create new texture based our camera settings
            out_textures[groupIndex] = new Texture2D(_rtCamera.targetTexture.width, _rtCamera.targetTexture.height, TextureFormat.ARGB32, true, false);

            // write rendertexture results to our new texture
            _rtCamera.targetTexture.GenerateMips();
            
            Graphics.CopyTexture(_rtCamera.targetTexture, out_textures[groupIndex]);
            
            // pass new texture into material shader
            BakeGroups[groupIndex].TargetMaterial.SetTexture(BakeGroups[groupIndex].MaterialShaderAttribute, out_textures[groupIndex]);

            // reset brightness
            BakeGroups[groupIndex].Set_PreviousBrightness();

            // disable gameObject group
            BakeGroups[groupIndex].gameObject.SetActive(false);

            // clear render texture and camera
            RenderTexture.ReleaseTemporary(in_renderTexture);
            
            _rtCamera.targetTexture = null;
            _rtCamera.enabled       = false;
        }
        public void BakeAllGroups()
        {
            if (BakeGroups != null && BakeGroups.Count > 0)
            {
                if (out_textures == null || out_textures.Length == 0)
                {
                    out_textures = new Texture2D[BakeGroups.Count];
                }

                for (int i = 0; i < BakeGroups.Count; i++)
                {
                    BakeGroup(i);
                }
            }
        }

        /// <summary>
        /// This starts a coroutine that delays the ability to trigger any texture baking
        /// </summary>
        public void StartBakeQueueLoop()
        {
            _queued = true;

            // Start loop if it hasn't already been started
            if (BakeLoop_Coroutine == null)
            {
                BakeLoop_Coroutine = StartCoroutine(BakeLoop());
            }
        }
        #endregion

        #region DELAY COROUTINES

        #region DELAYED BAKE QUEUE
        private Coroutine BakeLoop_Coroutine;
        private IEnumerator BakeLoop()
        {
            float bakeLoopWaitTime = 0.0f;

            #if UNITY_STANDALONE
                bakeLoopWaitTime = desktopDelayTime;
            #elif UNITY_ANDROID
                bakeLoopWaitTime = mobileDelayTime;
            #endif

            while(true)
            {
                if(_queued)
                {
                    BakeAllGroups();
                    _queued = false;
                }

                yield return Delay(bakeLoopWaitTime);
            }
        }
        #endregion

        public void StartDelayedBaking()
        {
            StartCoroutine(DelayedBaking());
        }

        public IEnumerator DelayedBaking()
        {
            yield return Delay(2.5f);

            BakeAllGroups();
        }

        public IEnumerator Delay(float delayTime)
        {
            yield return new WaitForSeconds(delayTime);
        }
        #endregion

        #region CREATE COMPONENTS
        private Camera CreateRTCamera()
        {
            if (_rtCamera != null) { return _rtCamera; }

            // create new Camera component
            _rtCamera                       = this.gameObject.AddComponent<Camera>();
            _rtCamera.clearFlags            = CameraClearFlags.SolidColor;
            _rtCamera.backgroundColor       = Color.black;
            _rtCamera.orthographic          = true;
            _rtCamera.orthographicSize      = 0.5f;
            _rtCamera.nearClipPlane         = 0.0f;
            _rtCamera.farClipPlane          = 0.25f;
            _rtCamera.useOcclusionCulling   = false;
            _rtCamera.allowHDR              = false;
            _rtCamera.allowMSAA             = false;
            _rtCamera.cullingMask           = cameraLayerMask;
            _rtCamera.stereoTargetEye       = 0;

            return _rtCamera;
        }
        private void CreateRenderTexture(BakedTextureResolution resolutionSetting)
        {
            switch (resolutionSetting)
            {
                case BakedTextureResolution.High:
                    _rtResolution = 4096;
                    break;
                case BakedTextureResolution.Medium:
                    _rtResolution = 2048;
                    break;
                case BakedTextureResolution.Low:
                    _rtResolution = 1024;
                    break;
                case BakedTextureResolution.UltraLow:
                    _rtResolution = 512;
                    break;
            }

            in_renderTexture = RenderTexture.GetTemporary(_rtResolution, _rtResolution, 16, RenderTextureFormat.ARGB32);
            in_renderTexture.useMipMap = true;
            in_renderTexture.autoGenerateMips = false;
            in_renderTexture.name = "Dynamically Generated Render Texture";
        }
        #endregion
    }
}