document.addEventListener("DOMContentLoaded", () => {
  const nav = document.getElementById("nav-placeholder");
  if (nav) {
    includeHTML("nav-placeholder", "./nav.html", () => {
      // Mark the current tab so the nav shows where you are.
      const page = nav.getAttribute("data-page");
      const link = nav.querySelector(`a[data-page="${page}"]`);
      if (link) link.classList.add("active");
    });
  }

  if (document.getElementById("footer-placeholder")) {
    includeHTML("footer-placeholder", "./footer.html");
  }
});

document.addEventListener("alpine:init", () => {
  const register = (name, fn) => {
    if (typeof fn !== "undefined") Alpine.data(name, fn);
  };

  register("themeSwitcher", typeof themeSwitcher !== "undefined" ? themeSwitcher : undefined);
  register("otaUploadHandler", typeof otaUploadHandler !== "undefined" ? otaUploadHandler : undefined);
  register("fileManager", typeof fileManager !== "undefined" ? fileManager : undefined);
  register("networkPage", typeof networkPage !== "undefined" ? networkPage : undefined);
  register("weatherPage", typeof weatherPage !== "undefined" ? weatherPage : undefined);
  register("timePage", typeof timePage !== "undefined" ? timePage : undefined);
  register("picturesPage", typeof picturesPage !== "undefined" ? picturesPage : undefined);
  register("settingsPage", typeof settingsPage !== "undefined" ? settingsPage : undefined);
});
