using UnityEditor;
using UnityEngine;

namespace VRR.TechArtTools.Menu
{
    public class MenuTab<T> : IMenuTab
    {
        protected T toolsScript;
        protected SerializedObject so;

        protected GUIStyle _labelField;
        protected GUIStyle _toggleField;
        protected GUIStyle _helpText;
        protected GUIStyle _header;
        protected GUIStyle _warningText;

        protected bool optionsFoldout;

        #region INTERFACE FUNCTIONS
        public void Update()
        {
            if (so.targetObject == null)
            {
                GetProperties();
            }

            so.Update();
        }

        public void Apply()
        {
            so.ApplyModifiedProperties();
        }

        public virtual void GetProperties()
        {
            // serialize your script and
            // add your properties here
        }

        public void SetStyles(GUIStyle labelField, GUIStyle toggleField, GUIStyle helpText, GUIStyle header, GUIStyle warningText)
        {
            _labelField = labelField;
            _toggleField = toggleField;
            _helpText = helpText;
            _header = header;
            _warningText = warningText;
        }

        public virtual void DrawMenu(GUIStyle style)
        {
            // add your menus here
        }
        #endregion
    }
}