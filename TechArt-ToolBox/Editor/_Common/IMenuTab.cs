using UnityEngine;

namespace VRR.TechArtTools.Menu
{    
    public interface IMenuTab
    {
        void Update();

        void Apply();

        void GetProperties();

        void DrawMenu(GUIStyle style);
    }
}