using System.Security.Cryptography;
using System.Text;

namespace MuLauncher.Services;

public static class PasswordProtector
{
    public static string Protect(string password)
    {
        if (password.Length == 0) return "";
        if (!OperatingSystem.IsWindows())
            throw new PlatformNotSupportedException("Saved passwords currently use Windows account protection. Manual login works on other platforms.");
        var bytes = Encoding.Unicode.GetBytes(password + '\0');
        try { return Convert.ToHexString(ProtectedData.Protect(bytes, null, DataProtectionScope.CurrentUser)); }
        finally { CryptographicOperations.ZeroMemory(bytes); }
    }

    public static string Unprotect(string encrypted)
    {
        if (encrypted.Length == 0) return "";
        if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException("This password belongs to a Windows account.");
        var bytes = ProtectedData.Unprotect(Convert.FromHexString(encrypted), null, DataProtectionScope.CurrentUser);
        try { return Encoding.Unicode.GetString(bytes).TrimEnd('\0'); }
        finally { CryptographicOperations.ZeroMemory(bytes); }
    }
}
