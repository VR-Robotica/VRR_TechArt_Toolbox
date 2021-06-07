using System.Collections.Generic;
using System.IO;

using UnityEditor;
using UnityEngine;

using VRR.TechArtTools.Menu;
using VRR.TechArtTools.Utilities;

using VRR.TechArtTools.GeneralTools;
using VRR.TechArtTools.AnimationTools;
using VRR.TechArtTools.Lightmapping;
using VRR.TechArtTools.MeshTools;
using VRR.TechArtTools.Avatar;
using VRR.TechArtTools.BakingTools;

namespace VRR.TechArtTools
{
    public class EditorWindow_TechArtToolBox : EditorWindow
    {
        // Menu Tabs - The Tools!
        private MenuTab_GeneralTools    Menu_General;
        private MenuTab_AnimationTools  Menu_Animation;
        private MenuTab_BakerTools      Menu_BakerTool;
        private MenuTab_MeshTools       Menu_MeshTools;
        private MenuTab_LightmapTools   Menu_LightmapTool;
        private MenuTab_AvatarTools     Menu_Avatar;

        // basic menu properties
        private Texture2D           _logo;
        private List<MenuTab_Types> _menuTabsTypes;
        private List<string>        _menuTabLabels;
        private MenuTab_Types       _selectedTab;

        private GUIStyle            _styleLogo;
        private GUIStyle            _styleTitle;
        private GUIStyle            _styleTabs;

        private GUIStyle            labelField;
        private GUIStyle            toggleField;
        private GUIStyle            helpText;
        private GUIStyle            header;
        private GUIStyle            warningText;


        private Texture2D           _labelBG;
        private Font                _labelFont;

        private Texture2D           _tabNormal;
        private Texture2D           _tabSelected;
        private Font                _tabFont;

        private Vector2             _scrollPos;

        [MenuItem("VRR/TechArt ToolBox")]
        public static void ShowWindow()
        {
            EditorWindow.GetWindow(typeof(EditorWindow_TechArtToolBox), false, "TechArt ToolBox", true);
        }

        #region EDITOR WINDOW UPDATE
        private void OnEnable()
        {
            InitMenuTabs();
            InitStyles();

            Undo.undoRedoPerformed += OnUndoTriggered;
        }

        public void OnDestroy()
        {
            Undo.undoRedoPerformed -= OnUndoTriggered;
        }

        private void OnGUI()
        {
            UpdateSOs();

            DrawLogo();
            DrawStatistics();
            DrawMenuTabs();
            DrawMenu(_selectedTab);

            if (GUI.changed)
            {
                EditorUtility.SetDirty(this);
            }

            ModifySOs();
        }

        private void OnSelectionChange()
        {
            Repaint();
        }

        private void UpdateSOs()
        {
            /*
            if(Menu_Animation == null || Menu_BakerTool == null || Menu_MeshTools == null || Menu_LightmapTool == null || Menu_Rigging == null)
            {
                InitMenuTabs();
            }
            */

            // Update all Serialized Objects
            Menu_Animation.Update();
            Menu_BakerTool.Update();
            Menu_LightmapTool.Update();
            Menu_MeshTools.Update();
            Menu_Avatar.Update();
        }

        private void ModifySOs()
        {
            // Apply all modifications to all Serialized Objects
            Menu_Animation.Apply();
            Menu_BakerTool.Apply();
            Menu_LightmapTool.Apply();
            Menu_MeshTools.Apply();
            Menu_Avatar.Apply();
        }
        #endregion


        #region INITIALIZATION
        private void InitMenuTabs()
        {
            Menu_General        = new MenuTab_GeneralTools();
            Menu_Animation      = new MenuTab_AnimationTools();
            Menu_BakerTool      = new MenuTab_BakerTools();
            Menu_LightmapTool   = new MenuTab_LightmapTools();
            Menu_MeshTools      = new MenuTab_MeshTools();
            Menu_Avatar         = new MenuTab_AvatarTools();
        }

        private void InitStyles()
        {
            LoadResources();

            InitStyle_Logo();
            InitStyle_Heading();
            InitStyle_Tabs();
            InitStyle_Text();
        }

        private void InitStyle_Text()
        {
            labelField = new GUIStyle();
            labelField.fontSize = 14;
            labelField.alignment = TextAnchor.MiddleRight;

            toggleField = new GUIStyle();
            toggleField.wordWrap = true;
            toggleField.normal.textColor = new Color(0.9f, 0.9f, 0.9f);
            toggleField.alignment = TextAnchor.LowerLeft;

            helpText = new GUIStyle();
            helpText.fontSize = 10;
            helpText.alignment = TextAnchor.MiddleCenter;
            helpText.fontStyle = FontStyle.Italic;
            helpText.wordWrap = true;
            helpText.normal.textColor = new Color(0.6f, 0.6f, 0.6f);

            header = new GUIStyle();
            header.fontSize = 16;
            header.normal.textColor = new Color(0.55f, 1f, 0.8f, 1f);
            header.active.textColor = new Color(0.45f, .9f, 0.7f, 1f);
            header.alignment = TextAnchor.MiddleCenter;

            warningText = new GUIStyle();
            warningText.richText = true;
            warningText.wordWrap = true;
            warningText.fontStyle = FontStyle.Bold;
            warningText.alignment = TextAnchor.MiddleCenter;
            warningText.normal.textColor = new Color(0.9f, 0.1f, 0.1f);
        }

        private void LoadResources()
        {
            _logo           = (Texture2D)Resources.Load("Logo/LOGO");

            _labelBG        = (Texture2D)Resources.Load("GUIStyles/Backgrounds/labelBg");
            _labelFont      = (Font)Resources.Load("GUIStyles/Fonts/Orbitron Light");

            _tabNormal      = (Texture2D)Resources.Load("GUIStyles/Backgrounds/Tab_Default");
            _tabSelected    = (Texture2D)Resources.Load("GUIStyles/Backgrounds/Tab_Selected");
            _tabFont        = (Font)Resources.Load("GUIStyles/Fonts/Orbitron Light");
        }

        private void InitStyle_Logo()
        {
            // Menu LOGO
            _styleLogo                      = new GUIStyle();
            _styleLogo.alignment            = TextAnchor.MiddleCenter;
            _styleLogo.imagePosition        = ImagePosition.ImageOnly;
            _styleLogo.fixedHeight          = 64;
            _styleLogo.normal.background    = _tabNormal;
            _styleLogo.border               = new RectOffset(18, 18, 20, 4);
        }

        private void InitStyle_Heading()
        {
            // General Heading Labels
            _styleTitle                     = new GUIStyle();
            _styleTitle.alignment           = TextAnchor.MiddleCenter;
            _styleTitle.font                = _labelFont;
            _styleTitle.fontSize            = 16;
            _styleTitle.fixedHeight         = 24;
            _styleTitle.normal.background   = _labelBG;
            _styleTitle.normal.textColor    = Color.white;
            _styleTitle.border              = new RectOffset(18, 18, 20, 4);
        }

        private void InitStyle_Tabs()
        {
            // Menu Tabs
            _styleTabs                      = new GUIStyle();
            _styleTabs.alignment            = TextAnchor.MiddleCenter;
            _styleTabs.font                 = _tabFont;
            _styleTabs.fontSize             = 16;
            _styleTabs.fixedHeight          = 40;
            _styleTabs.normal.background    = _tabNormal;
            _styleTabs.normal.textColor     = Color.gray;

            _styleTabs.onNormal.background  = _tabSelected;
            _styleTabs.onNormal.textColor   = Color.white;

            _styleTabs.onFocused.background = _tabSelected;
            _styleTabs.onFocused.textColor  = Color.black;

            _styleTabs.border               = new RectOffset(18, 18, 20, 4);

            _menuTabsTypes                  = CustomUtilities.GetListFromEnum<MenuTab_Types>();
            _menuTabLabels                  = new List<string>();

            foreach (MenuTab_Types tab in _menuTabsTypes)
            {
                _menuTabLabels.Add(tab.ToString());
            }
        }
        #endregion


        #region MENU BASE
        private void DrawLogo()
        {
            GUILayout.Label(_logo, _styleLogo);
        }

        private void DrawStatistics()
        {
            Menu_General.DrawMenu_DisplayVertCount(_styleTitle);
        }

        private void DrawMenuTabs()
        {
            int index = (int)_selectedTab;

            EditorGUILayout.Space();

            index = GUILayout.Toolbar(index, _menuTabLabels.ToArray(), _styleTabs);

            _selectedTab = _menuTabsTypes[index];
        }
        
        private void DrawMenu(MenuTab_Types selectedTab)
        {
            _scrollPos = EditorGUILayout.BeginScrollView(_scrollPos, false, false);

            switch (selectedTab)
            {
                case MenuTab_Types.General:
                    Menu_General.DrawMenu(_styleTitle);
                    break;

                case MenuTab_Types.Animation:
                    Menu_Animation.DrawMenu(_styleTitle);
                    Menu_Animation.SetStyles(labelField, toggleField, helpText, header, warningText);
                    break;

                case MenuTab_Types.Meshes:                   
                    Menu_MeshTools.DrawMenu(_styleTitle);
                    Menu_MeshTools.SetStyles(labelField, toggleField, helpText, header, warningText);
                    break;

                case MenuTab_Types.Lightmaps:
                    Menu_LightmapTool.DrawMenu(_styleTitle);
                    break;

                case MenuTab_Types.Baking:
                    Menu_BakerTool.DrawMenu(_styleTitle);
                    Menu_BakerTool.SetStyles(labelField, toggleField, helpText, header, warningText);
                    break;

                case MenuTab_Types.Avatar:
                    Menu_Avatar.DrawMenu(_styleTitle);
                    break;

                default:
                    DrawMenu_Default();
                    break;
            }

            EditorGUILayout.EndScrollView();
        }
        #endregion

        private void DrawMenu_Default()
        {
            EditorGUILayout.BeginVertical("Box");

            EditorGUILayout.LabelField("Comming Soon...");

            EditorGUILayout.Space();

            EditorGUILayout.EndVertical();
        }

        private void OnUndoTriggered()
        {
            AssetDatabase.SaveAssets();
        }

    }
}