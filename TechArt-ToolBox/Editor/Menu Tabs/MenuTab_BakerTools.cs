using UnityEditor;
using UnityEngine;

using VRR.TechArtTools.Menu;
using VRR.TechArtTools.Utilities;

namespace VRR.TechArtTools.BakingTools
{
    public class MenuTab_BakerTools : MenuTab<Tools_Baking>
    {
        SerializedProperty Baker;
        SerializedProperty AtlasDivisions;
        SerializedProperty GroupName;
        SerializedProperty ShaderName;
        SerializedProperty GenerateMaterials;

        enum Channels { RED, GREEN, BLUE, ALPHA }
        float _defaultRedValue;
        float _defaultGreenValue;
        float _defaultBlueValue;
        float _defaultAlphaValue;

        Texture2D RedChannel;
        Texture2D GreenChannel;
        Texture2D BlueChannel;
        Texture2D AlphaChannel;

        Vector2Int _redSize;
        Vector2Int _greenSize;
        Vector2Int _blueSize;
        Vector2Int _alphaSize;
        Vector2Int outputSize;

        private string[] _shaderNames;
        private int _shaderIndex;

        public MenuTab_BakerTools()
        {
            GetProperties();
        }

        #region INTERFACE FUNCTIONS
        public override void GetProperties()
        {
            toolsScript = ScriptableObject.CreateInstance<Tools_Baking>();
            so = new SerializedObject(toolsScript);

            Baker               = so.FindProperty("_baker");
            AtlasDivisions      = so.FindProperty("_atlasDivision");
            GroupName           = so.FindProperty("_groupName");
            ShaderName          = so.FindProperty("_shaderName");
            GenerateMaterials   = so.FindProperty("_willGenerateMaterials");
        }

        public void SetStyles(GUIStyle labelField, GUIStyle toggleField, GUIStyle helpText, GUIStyle header, GUIStyle warningText)
        {
            _labelField = labelField;
            _toggleField = toggleField;
            _helpText = helpText;
            _header = header;
            _warningText = warningText;
        }

        public override void DrawMenu(GUIStyle style)
        {
            DrawMenu_Baker(style);

            DrawButton_Bake();

            DrawMenu_GenerateBaker(style);

            DrawMenu_TexturePacker(style);

            EditorGUILayout.Space();

        }
        #endregion

        #region MENU CALLS

        #region BAKER
        private void DrawMenu_Baker(GUIStyle style)
        {            
            EditorGUILayout.BeginVertical("Box");

                EditorGUILayout.LabelField("Setup Runtime Texture Baker", style);

                EditorGUILayout.Space();

                EditorGUILayout.BeginHorizontal();
                    EditorGUILayout.PropertyField(Baker, true);

                    if (GUILayout.Button("Find Baker"))
                    {
                        if (Baker.objectReferenceValue == null)
                        {
                            toolsScript.SetBaker(GameObject.FindObjectOfType<TextureBaker>());
                        }
                    }
                EditorGUILayout.EndHorizontal();

            EditorGUILayout.EndVertical();

            EditorGUILayout.Space();
        }
        private void DrawButton_Bake()
        {
            if (Baker.objectReferenceValue == null) { return; }


            EditorGUILayout.BeginVertical();

                if (GUILayout.Button("Bake All Groups"))
                {
                    toolsScript.Bake();
                }

            EditorGUILayout.EndVertical();

            EditorGUILayout.Space();
        }
        private void DrawMenu_GenerateBaker(GUIStyle style)
        {
            _shaderNames = CustomEditorUtilities.GetAllShaderNames();

            EditorGUILayout.BeginVertical("Box");

                EditorGUILayout.LabelField("Generate Bake Group", style);

                EditorGUILayout.Space();

                EditorGUILayout.PropertyField(GroupName);
                EditorGUILayout.PropertyField(AtlasDivisions);

                EditorGUILayout.Space();

                EditorGUILayout.PropertyField(GenerateMaterials, new GUIContent("Generate Materials?"));

                if (GenerateMaterials.boolValue)
                {
                    _shaderIndex            = EditorGUILayout.Popup("Shader: ", _shaderIndex, _shaderNames);
                    ShaderName.stringValue  = _shaderNames[_shaderIndex];

                    EditorGUILayout.LabelField("[" + _shaderIndex + "] " + ShaderName.stringValue);
                }

            EditorGUILayout.EndVertical();

            if (GUILayout.Button("Generate"))
            {
                toolsScript.GenerateBakeGroup();
            }

            EditorGUILayout.Space();
        }
        #endregion

        #region PACKER
        private void DrawMenu_TexturePacker(GUIStyle style)
        {
            EditorGUILayout.BeginVertical("Box");

                EditorGUILayout.LabelField("Texture Packer", style);

            EditorGUILayout.EndVertical();

            EditorGUILayout.Space();

            EditorGUILayout.LabelField("Coming Soon...");
            return;

            //DrawReset();

            //EditorGUILayout.BeginVertical();

            //    EditorGUILayout.BeginHorizontal("box", GUILayout.Height(60));
            //        DrawChannelInput(Channels.RED);
            //    EditorGUILayout.EndHorizontal();

            //    EditorGUILayout.BeginHorizontal("box", GUILayout.Height(60));
            //        DrawChannelInput(Channels.GREEN);
            //    EditorGUILayout.EndHorizontal();

            //    EditorGUILayout.BeginHorizontal("box", GUILayout.Height(60));
            //        DrawChannelInput(Channels.BLUE);
            //    EditorGUILayout.EndHorizontal();

            //    EditorGUILayout.BeginHorizontal("box", GUILayout.Height(60));
            //        DrawChannelInput(Channels.ALPHA);
            //    EditorGUILayout.EndHorizontal();

            //EditorGUILayout.EndVertical();

            //EditorGUILayout.Space();

            //DisplayPreview();

            //if (GUILayout.Button("Pack Texture!"))
            //{

            //}
        }
        private void DrawChannelInput(Channels channel)
        {
            switch(channel)
            {
                case Channels.RED:
                    EditorGUILayout.BeginVertical();
                    RedChannel = (Texture2D)EditorGUILayout.ObjectField("RED Channel:", RedChannel, typeof(Texture2D), false);
                    if(!RedChannel)
                    {
                        GUILayout.Label("No texture set, use slider to set default value");
                        _defaultRedValue = EditorGUILayout.Slider(_defaultRedValue, 0f, 1f);
                    }
                    else
                    {
                        _redSize = GetTextureSize(RedChannel);
                        GUILayout.Label("Size: " + _redSize.x + " x " + _redSize.y);
                    }
                    EditorGUILayout.EndVertical();
                    break;

                case Channels.GREEN:
                    EditorGUILayout.BeginVertical();
                    GreenChannel = (Texture2D)EditorGUILayout.ObjectField("GREEN Channel:", GreenChannel, typeof(Texture2D), false);
                    if (!GreenChannel)
                    {
                        GUILayout.Label("No texture set, use slider to set default value");
                        _defaultGreenValue = EditorGUILayout.Slider(_defaultGreenValue, 0f, 1f);
                    }
                    else
                    {
                        _greenSize = GetTextureSize(GreenChannel);
                        GUILayout.Label("Size: " + _greenSize.x + " x " + _greenSize.y);
                    }
                    EditorGUILayout.EndVertical();
                    break;

                case Channels.BLUE:
                    EditorGUILayout.BeginVertical();
                    BlueChannel = (Texture2D)EditorGUILayout.ObjectField("BLUE Channel:", BlueChannel, typeof(Texture2D), false);
                    if (!BlueChannel)
                    {
                        GUILayout.Label("No texture set, use slider to set default value");
                        _defaultBlueValue = EditorGUILayout.Slider(_defaultBlueValue, 0f, 1f);
                    }
                    else
                    {
                        _blueSize = GetTextureSize(BlueChannel);
                        GUILayout.Label("Size: " + _blueSize.x + " x " + _blueSize.y);
                    }
                    EditorGUILayout.EndVertical();
                    break;
                
                case Channels.ALPHA:
                    EditorGUILayout.BeginVertical();
                    AlphaChannel = (Texture2D)EditorGUILayout.ObjectField("ALPHA Channel:", AlphaChannel, typeof(Texture2D), false);
                    if (!AlphaChannel)
                    {
                        GUILayout.Label("No texture set, use slider to set default value");
                        _defaultAlphaValue = EditorGUILayout.Slider(_defaultAlphaValue, 0f, 1f);
                    }
                    else
                    {
                        _alphaSize = GetTextureSize(AlphaChannel);
                        GUILayout.Label("Size: " + _alphaSize.x + " x " + _alphaSize.y);
                    }
                    EditorGUILayout.EndVertical();
                    break;
            }
        }
        private void DisplayPreview()
        {
            if (outputSize != null && outputSize != Vector2Int.zero)
            {
                GUILayout.Label("Output Size: " + outputSize.x + " x " + outputSize.y, _helpText);
            }
        }

        private Vector2Int GetTextureSize(Texture2D tex)
        {
            if(outputSize == null || outputSize == Vector2Int.zero || outputSize.x < tex.width || outputSize.y < tex.height)
            {
                outputSize = new Vector2Int(tex.width, tex.height);
            }

            if(outputSize.x != tex.width || outputSize.y != tex.height)
            {
                GUILayout.Label("Texture has a different size than our output.", _warningText);
            }

            return new Vector2Int(tex.width, tex.height);
        }
        #endregion


        private void DrawExportPath()
        {

        }
        private void DrawReset()
        {
            EditorGUILayout.Space();

            EditorGUILayout.BeginVertical("box");

            if (GUILayout.Button("Reset"))
            {
                RedChannel = null;
                _defaultRedValue = 0;

                GreenChannel = null;
                _defaultGreenValue = 0;

                BlueChannel = null;
                _defaultBlueValue = 0;

                AlphaChannel = null;
                _defaultAlphaValue = 0;

                outputSize = Vector2Int.zero;
            }

            EditorGUILayout.EndVertical();

            EditorGUILayout.Space();
        }
        #endregion
    }
}