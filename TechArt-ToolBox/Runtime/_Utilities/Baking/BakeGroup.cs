using UnityEngine;

namespace VRR.TechArtTools.BakingTools
{
    [ExecuteInEditMode]
    [System.Serializable]
    public class BakeGroup : MonoBehaviour
    {
        [SerializeField] public BakedTextureResolution Resolution = BakedTextureResolution.High;
        [SerializeField] public GameObject  SourcePrefab;
        [SerializeField] public Material[]  SourceMaterials;
        [SerializeField] public Material    TargetMaterial;
        [SerializeField] public string      MaterialShaderAttribute = "_MainTex"; // default name on most shaders
        [SerializeField] public Texture2D   DefaultTexture;
        
        private Material[] _sourceMaterials;
        private Material   _targetMaterial;

       
        
        public void ResetMaterials()
        {
            SourceMaterials = null;
        }

        public void Get_Materials()
        {
            Renderer[] renderers = this.gameObject.GetComponentsInChildren<Renderer>();

            SourceMaterials = new Material[renderers.Length];

            for(int i = 0; i < renderers.Length; i++)
            {
                SourceMaterials[i] = renderers[i].sharedMaterial;
            }
        }

        public void Set_DefaultTexture()
        {
            // make sure group has target material assigned...
            if (TargetMaterial != null)
            {
                // make sure group has shader attribute assigned...
                if (MaterialShaderAttribute != null && MaterialShaderAttribute != "")
                {
                    TargetMaterial.SetTexture(MaterialShaderAttribute, DefaultTexture);
                }
                else
                {
                    Debug.Log(this.gameObject.name + "'s MaterialShaderAttribute has not been set!");
                }
            }
            else
            {
                Debug.Log(this.gameObject.name + "'s TargetMaterial has not been set!");
            }
        }



        float[] previousBrightness;

        public void Set_BaseBrightness()
        {
            string shaderAttribute = "_BrightnessOverride";

            previousBrightness = new float[SourceMaterials.Length];

            for (int i = 0; i < SourceMaterials.Length; i++)
            {
                if (SourceMaterials[i].HasProperty(shaderAttribute))
                {
                    previousBrightness[i] = SourceMaterials[i].GetFloat(shaderAttribute);

                    SourceMaterials[i].SetFloat(shaderAttribute, 0.0f);
                }
                else
                {
                    Debug.Log(shaderAttribute + " was not found in this shader [" + SourceMaterials[i].shader.name + "]");
                }
            }
        }

        public void Set_PreviousBrightness()
        {
            string shaderAttribute = "_BrightnessOverride";

            for (int i = 0; i < SourceMaterials.Length; i++)
            {
                if (SourceMaterials[i].HasProperty(shaderAttribute))
                {
                    SourceMaterials[i].SetFloat(shaderAttribute, previousBrightness[i]);
                }
            }
        }
    }
}